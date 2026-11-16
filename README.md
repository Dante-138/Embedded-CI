# lib-embedded-c-ci

Reusable C11 modules for microcontroller firmware, tested on the PC with
AddressSanitizer, UndefinedBehaviorSanitizer and cppcheck on every push.

Every module follows the same rules:

- C11, no dynamic allocation, no vendor SDK: it drops into any firmware (Zephyr,
  ESP-IDF, bare metal) as one `.c` and one `.h`.
- Built with warnings as errors (`-Wall -Wextra -Wpedantic -Wconversion
  -Wsign-conversion -Wshadow` and more) and with ASan + UBSan, so an
  out-of-bounds access or undefined behavior fails the test run.
- Tested on the host first: the logic is proven before it touches a board.

## Modules

| Module | What it does | Problem it solves | How it is tested |
| --- | --- | --- | --- |
| [`frame`](src/frame.h) | Byte-by-byte parser and encoder for a serial frame: `SOF`, `LEN`, payload, CRC-16 | Lost or misaligned frames on a UART link, and code that needs a flush or reset after line noise | 16 host tests (below) with ASan + UBSan at three payload limits, plus cppcheck |

Every other module has its own page in [docs/](docs/): what it does, the problem it
solves, how to use it and how it is tested.

## frame

### Wire format

```text
+------+-----+------------------+--------+--------+
| SOF  | LEN | PAYLOAD[LEN]     | CRC_HI | CRC_LO |
+------+-----+------------------+--------+--------+
  0xA5   1 B   0..FRAME_MAX_PAYLOAD (default 64)
```

The CRC is CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`) over `LEN` and the
payload, sent high byte first.

### Behavior

- One byte in, one status out: `FRAME_PENDING`, `FRAME_READY`,
  `FRAME_ERROR_LENGTH` or `FRAME_ERROR_CRC`.
- Resynchronizes by itself: after any error it goes back to hunting for `SOF`.
  Whatever state noise leaves it in, at most `FRAME_MAX_PAYLOAD + 3` bytes
  without `SOF` bring it back to hunting.
- A repeated `SOF` is taken as a new start, so a stray `SOF` before a real frame
  does not cost that frame. This applies only while `FRAME_MAX_PAYLOAD < 0xA5`;
  from `0xA5` up, `SOF` is also a valid `LEN` and is read as one.
- No backtracking, by design (O(1) memory, no second buffer): a false `SOF` in
  line noise can make the parser read the start of the next real frame as
  payload, and that frame is lost. Measured by the tests with 0 to 16 random
  bytes (`SOF` included) before each of 20,000 frames:

  | `FRAME_MAX_PAYLOAD` | Frames lost | Worst run of lost frames | Frames delivered wrong in this run |
  | --- | --- | --- | --- |
  | 64 | 181 (0.9%) | 4 | 0 |
  | 165 | 835 (4.2%) | 7 | 0 |
  | 255 | 1,180 (5.9%) | 7 | 0 |

  The zero in the last column holds for this run (fixed seed), not in general.
  A 16-bit CRC accepts a random false frame with a chance of about 1 in 65,536,
  so on a long enough noisy link a frame made of noise will eventually come out
  as `FRAME_READY`. If the application cannot accept that, it should validate
  the payload itself (command codes, ranges, a sequence number) or use a
  stronger check.

  A smaller limit loses less, because a false start swallows fewer bytes. On a
  link that is noisy and has idle gaps, calling `frame_parser_init()` on the
  idle timeout drops a false start early.
- No timing inside: if the link has an idle timeout, call `frame_parser_init()`
  when it fires to drop a half-received frame.
- The payload pointer is valid only right after `FRAME_READY`, until the next
  byte is fed. Asking at any other time returns `NULL`.

### Usage

```c
#include "frame.h"

static frame_parser_t parser;

void link_init(void)
{
    frame_parser_init(&parser);
}

/* Call for every byte taken from the UART RX buffer (main loop or task, not the ISR). */
void link_on_byte(uint8_t byte)
{
    switch (frame_parser_feed(&parser, byte)) {
    case FRAME_READY: {
        uint8_t length;
        const uint8_t *payload = frame_parser_payload(&parser, &length);
        handle_command(payload, length);
        break;
    }
    case FRAME_ERROR_LENGTH:
    case FRAME_ERROR_CRC:
        link_errors++; /* the parser has already resynchronized */
        break;
    default:
        break;
    }
}

/* Sending: */
uint8_t out[FRAME_MAX_PAYLOAD + FRAME_OVERHEAD];
size_t size = frame_encode(data, data_length, out, sizeof out); /* 0 on bad arguments */
```

### Tests ([tests/test_frame.c](tests/test_frame.c))

- CRC against the published check value: `"123456789"` gives `0x29B1`, also
  when computed in two parts.
- Encoder byte layout, and rejection of a buffer too small, a `NULL` pointer
  and a length above the limit.
- Round trip for every payload length from 0 to `FRAME_MAX_PAYLOAD`: every byte
  but the last returns `FRAME_PENDING`, the last returns `FRAME_READY`.
- A single flipped bit in each payload and CRC byte is reported as
  `FRAME_ERROR_CRC`, and the next good frame goes through.
- Length above the limit, leading garbage, a repeated `SOF`, three frames back
  to back, and a frame cut in half followed by `frame_parser_init()`.
- A corrupted parser state restarts cleanly instead of indexing out of bounds.
- 200,000 random bytes: no crash, no sanitizer report, and no frame over the
  length limit.
- 2,000 random frames with random noise without `SOF` between them: every frame
  comes out intact.
- Resynchronization bound: 2,000 rounds of noise with `SOF` (half of them
  ending on a forced false start), then `FRAME_MAX_PAYLOAD + 3` bytes without
  `SOF`: the next frame always parses.
- 20,000 frames with noise that includes `SOF`: no frame delivered with the
  wrong content in this run (see the CRC limit above), and the loss stays under
  10% (table above).

The whole suite runs three times, with `FRAME_MAX_PAYLOAD` at 64 (default), 165
(the first value where `SOF` is also a valid `LEN`) and 255 (the largest `LEN`).

## Running the tests

On Linux, with gcc and cppcheck installed:

```bash
make test proof analyze
```

On Windows, in the same container image as CI (Docker Desktop running), from
the repository folder:

```bash
docker run --rm -v "$PWD:/src" -w /src gcc:14@sha256:a6f425a380ee03e682df3b436fc9077c544fb0fea0a9cab943c3b8320db10765 make test proof
```

In Git Bash, prefix it with `MSYS_NO_PATHCONV=1`.

| Target | What it does |
| --- | --- |
| `make test` | builds with ASan + UBSan and runs the tests once per payload limit (64, 165, 255) |
| `make proof` | builds two programs with a deliberate bug each, a heap overflow only ASan sees and a signed overflow only UBSan sees, and fails unless each sanitizer reports its own bug, so neither can be silently turned off |
| `make analyze` | cppcheck over `src` and `tests` |
| `make clean` | removes `build/` |

## CI

| Workflow | Check | What it runs |
| --- | --- | --- |
| [`ci.yml`](.github/workflows/ci.yml) | Host tests | `make test`, `make proof` and `make analyze` in `gcc:14`, pinned by digest |
| [`pr.yml`](.github/workflows/pr.yml) | PR text | [`tools/check-commits.ts`](tools/check-commits.ts): Conventional Commits, subject up to 72 characters, no co-author lines, no e-mail addresses, PR targets `main` |

Actions are pinned by commit SHA and the workflow token is read-only.

## License

[MIT](LICENSE)
