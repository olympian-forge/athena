# C++/Python Model Contract

The neural network is defined and trained in Python (`training/model.py`,
`training/train.py`) but consumed at inference time by the C++ engine
(`src/nn/nn.cpp`, `src/mcts/mcts.cpp`) via an exported ONNX graph. Nothing
enforces that the two sides agree — this document is that agreement,
written down once instead of scattered across comments in three files, so
future changes can be checked against it instead of silently drifting
apart. If you change encoding logic on either side, update both sides and
this document in the same commit.

## Input tensor: 14x8x8, float32

Built by `NN::board_to_tensor` (`src/nn/nn.cpp`) and
`ChessDataset.fen_to_tensor` (`training/train.py`). Both must produce
bit-for-bit identical tensors for the same FEN.

| Channels | Content |
| --- | --- |
| 0-5 | White piece bitboards: pawn, knight, bishop, rook, queen, king (in that order) |
| 6-11 | Black piece bitboards: pawn, knight, bishop, rook, queen, king (in that order) |
| 12 | Side to move, broadcast across all 64 squares: `+1.0` if white to move, `-1.0` if black |
| 13 | Castling rights, broadcast across all 64 squares (see below) |

Channels 0-11 are one-hot per square: `1.0` where that piece type/color
occupies the square, `0.0` elsewhere. The channel order matches
`chess::Board::get_bitboard_index` (`include/chess/board.h`) exactly:
`P,N,B,R,Q,K,p,n,b,r,q,k` — and matches python-chess's own
`piece_type` numbering (`PAWN=1 .. KING=6`) offset by 0 (white) or 6
(black) on the Python side, so the two orderings agree without either
side needing to reference the other's convention explicitly.

**Channel 13 (castling) is a real contract fragility, not an oversight**:
it encodes only the *first character* of the FEN castling field as its
raw ASCII/byte value — `float(ord(castling_rights[0])) if len(...) > 0
else 0.0` on the Python side, `static_cast<float>(static_cast<unsigned
char>(castling_rights[0]))` (else `0.0f`) in C++. This is fragile by
construction (e.g. `"Qkq"` and `"KQkq"` encode differently even though
both permit white queenside castling) but it is what the network was
actually trained against — do not "fix" the encoding on one side without
retraining and updating both sides together.

## ONNX graph I/O

Exported by `train.py`'s `torch.onnx.export(...)` call and read by
`NN::evaluate_batch` (`src/nn/nn.cpp`) via matching literal tensor names:

| Tensor | Name | Shape |
| --- | --- | --- |
| Input | `"input"` | `(batch, 14, 8, 8)` |
| Output | `"policy"` | `(batch, 4672)` |
| Output | `"value"` | `(batch, 1)` |

The batch dimension is dynamic on export (`dynamic_axes` in `train.py`),
so any batch size works at inference time. `POLICY_SIZE = 4672`
(`include/nn/nn.h`) must match the policy head's output width exactly —
`NN::evaluate_batch` treats a mismatch as a hard error, not a
best-effort reshape.

Training resumes from / exports to two files, always written together:
`onnx/athena.onnx` (used by the C++ engine) and `onnx/athena.pth` (a raw
PyTorch state dict, used by `train.py` itself to resume). `PyTorch >=
2.9` writes ONNX weights to a side-car `.onnx.data` file by default,
which the C++ engine's memory-buffer session loader cannot resolve —
`train.py` consolidates the graph into one self-contained file
(`onnx.save_model(..., save_as_external_data=False)`) before the atomic
rename that makes it live.

## Policy head: 4672 = 64 squares x 73 move-planes

Encoded identically by `move_index` (anonymous namespace, top of
`src/mcts/mcts.cpp`) and `uci_to_index` (`training/train.py`):

```
index = (from_square * 73) + channel
```

`from_square` is `rank * 8 + file`, `0` = a1 through `63` = h8 (standard
python-chess / UCI square numbering) — both sides agree on this because
each computes it independently from FEN/UCI text using the same rank/file
arithmetic, not by sharing a lookup table.

`channel` is one of three disjoint ranges, chosen by move shape:

- **`0-55`: queen-like moves** (regular king/queen/rook/bishop/pawn
  moves, *including queen promotions* — see note below). `channel = dir_idx * 7 + (distance - 1)`,
  `distance` in `1..7`. `dir_idx` is the 8-way compass direction of the
  move, in this exact order (both sides list it identically):
  `N=0, NE=1, E=2, SE=3, S=4, SW=5, W=6, NW=7`.
- **`56-63`: knight moves.** `channel = 56 + i`, where `i` indexes this
  exact offset list: `(1,2), (2,1), (2,-1), (1,-2), (-1,-2), (-2,-1),
  (-2,1), (-1,2)` (as `(dx, dy)` in files/ranks).
- **`64-72`: non-queen promotions only.** `channel = 64 + (promo_dir *
  3) + promo_type`, where `promo_dir = dx + 1` (`0` = capture toward the
  a-file, `1` = straight, `2` = capture toward the h-file) and
  `promo_type` is `0` = knight, `1` = bishop, `2` = rook.

**Queen promotions are not distinguished from a normal move in the
encoding.** A pawn push or capture that happens to promote to a queen
falls through to the `0-55` queen-like range exactly like a non-promoting
move in the same direction — the network has no explicit "this is a
promotion" signal for the queen case, only for underpromotions. This is
intentional (mirrors the standard AlphaZero 73-plane action space) but
easy to misread as a bug if you're not expecting the asymmetry.

## Self-play JSONL schema

Written by `SelfPlay::run` (`src/selfplay/selfplay.cpp`) to
`data/selfplay/temp_gpu<id>_pid<pid>_<timestamp>.jsonl`, one JSON object
per line, batched 5 games per file, then atomically renamed to
`READY_...jsonl` once the batch is fully written. Consumed and deleted
(archived 1-in-`ARCHIVE_EVERY`, default 20) by `ChessDataset` in
`train.py`, which only ever reads files whose name starts with `READY_`.

Each line:

```json
{"fen": "<FEN before the move was played>", "result": <float, 0.0-1.0>, "policy": {"<uci-move>": <probability>, ...}}
```

- `fen` is the position *before* the recorded move, not after.
- `policy` keys are UCI move strings (`Move::to_uci_notation()`, e.g.
  `"e2e4"`, `"b7b8q"`) for exactly the legal moves at that position — this
  is the search's raw policy filtered down to legality and renormalized,
  not the full 4672-wide vector.
- `result` is from the perspective of the side to move **at that
  position**, not always White's perspective:
  `res = white_result` if White was to move there, else `1.0 -
  white_result`. `train.py` renormalizes this from `[0, 1]` to `[-1, 1]`
  for the value head's tanh output at load time — the stored JSONL value
  is always `[0, 1]`.
- `result` may additionally carry a temporal discount toward `0.5`
  (`result_discount`, default `0.99`) as a function of how many plies a
  position was from the game's actual end, so positions far from the
  outcome are labeled less confidently than positions right before it.
  This is applied before writing, so the JSONL `result` field is already
  discounted — it is not the raw terminal outcome broadcast unchanged to
  every position in the game.
