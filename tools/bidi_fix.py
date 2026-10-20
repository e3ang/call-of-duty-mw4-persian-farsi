"""Fix punctuation placement for Persian text in a left-to-right text engine.

The game lays out every line left-to-right (the game language stays English),
so punctuation at the start or end of a Persian line is not attached to any
Persian letter and ends up on the wrong side. Two strategies:

  rlm   - wrap each Persian line in U+200F (RIGHT-TO-LEFT MARK), so edge
          punctuation is resolved as right-to-left.
  move  - move sentence-final punctuation to the start of the stored line,
          so it is drawn on the left (the end of a Persian sentence).

The translation files keep normal logical order; this runs at build time.
"""
import re

RLM = '‏'
PERSIAN = re.compile('[؀-ۿ]')
CODES = r'(?:\^\?#x[0-9a-fA-F]+\.#x[0-9a-fA-F]+\^?|\^[0-9A-Za-z%+$])*'
TAIL = re.compile(r'([.!?؟…:؛،]+)(' + CODES + r')$')


def fix_line(line, mode):
    if not PERSIAN.search(line):
        return line
    if mode == 'rlm':
        return RLM + line + RLM
    if mode == 'move':
        m = TAIL.search(line)
        if m:
            return m.group(1) + line[:m.start()] + m.group(2)
        return line
    raise ValueError(mode)


def fix(text, mode):
    if not mode:
        return text
    return '\n'.join(fix_line(l, mode) for l in text.split('\n'))
