# SPDX-License-Identifier: MIT
# Copyright (c) 2026 redactd
# Reports how much of the firmware slot the image uses (the linker already refuses anything that
# would overlap the drive's journal; this is the readable summary).
file(SIZE ${BIN} size)
math(EXPR pct "${size} * 100 / ${LIMIT}")
message(STATUS "firmware image: ${size} bytes (${pct}% of the ${LIMIT}-byte firmware slot)")
