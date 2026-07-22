# Copico Centipede Console Keyboard Map

The Color Computer 2 keyboard has 53 keys but standard ASCII has 95 printable
characters.  The Copico Centipede firmware uses the **CLEAR** key as a modifier
(alongside **SHIFT**) to make all 95 printable ASCII characters accessible.

## Modifier Keys

| Modifier | Physical Key | Position in Matrix |
|----------|-------------|-------------------|
| SHIFT    | Right-hand side | PB7 / PA6 |
| CLEAR    | Above BREAK     | PB1 / PA6 |

Four keyboard states are possible: **No modifier**, **SHIFT**, **CLEAR**,
and **SHIFT+CLEAR**.

## Standard Keys (No Modifier)

Default mode produces lowercase letters and basic punctuation.

```
@  h  p  x  0  8  ENTER
a  i  q  y  1  9  [CLEAR]
b  j  r  z  2  :  BREAK
c  k  s  ↑  3  ;
d  l  t  ↓  4  ,
e  m  u  ←  5  -
f  n  v  →  6  .
g  o  w  SP 7  /
```

## SHIFT Keys

Produces uppercase letters and shifted punctuation.

```
@  H  P  X  _  (  ENTER
A  I  Q  Y  !  )  [CLEAR]
B  J  R  Z  "  *  BREAK
C  K  S  ↑  #  +
D  L  T  ↓  $  <
E  M  U  ←  %  =
F  N  V  →  &  >
G  O  W  SP '  ?
```

## CLEAR Keys (Special Characters)

Hold **CLEAR** to access brackets, backslash, caret, and other
symbols.  Marked positions match NitrOS-9 conventions where possible.

```
`  h  p  x  0  [  ENTER       CLEAR + @  →  `  (backtick)
a  i  q  y  |  ]  [CLEAR]     CLEAR + 1  →  |  (pipe)
b  j  r  z  2  :  BREAK       CLEAR + 3  →  ~  (tilde)
c  k  s  ↑  ~  ;              CLEAR + 7  →  ^  (caret)
d  l  t  ↓  4  {              CLEAR + 8  →  [  (left bracket)
e  m  u  ←  5  _              CLEAR + 9  →  ]  (right bracket)
f  n  v  →  6  }              CLEAR + ,  →  {  (left brace)
g  o  w  SP ^  \              CLEAR + .  →  }  (right brace)
                               CLEAR + -  →  _  (underscore)
                               CLEAR + /  →  \  (backslash)
```

## SHIFT+CLEAR Keys

Hold **SHIFT+CLEAR** for alternate access to braces, pipe, and tilde.

```
@  H  P  X  _  {  ENTER       SH+CLR + 8  →  {  (left brace)
A  I  Q  Y  !  }  [CLEAR]     SH+CLR + 9  →  }  (right brace)
B  J  R  Z  "  *  BREAK       SH+CLR + 7  →  ~  (tilde)
C  K  S  ↑  #  +              SH+CLR + /  →  |  (pipe)
D  L  T  ↓  $  <
E  M  U  ←  %  =
F  N  V  →  &  >
G  O  W  SP ~  |
```

## Quick Reference: Special Characters

All printable ASCII characters not found on the unshifted or shifted
CoCo keyboard:

| Character | Name           | Key Combo     | NitrOS-9 |
|-----------|----------------|---------------|----------|
| `[`       | Left bracket   | CLEAR + 8     | Same     |
| `]`       | Right bracket  | CLEAR + 9     | Same     |
| `{`       | Left brace     | CLEAR + ,     | Same     |
| `}`       | Right brace    | CLEAR + .     | Same     |
| `\`       | Backslash      | CLEAR + /     | Same     |
| `\|`      | Pipe           | CLEAR + 1     | Same     |
| `^`       | Caret          | CLEAR + 7     | Same     |
| `~`       | Tilde          | CLEAR + 3     | Same     |
| `_`       | Underscore     | CLEAR + -     | Same     |
| `` ` ``   | Backtick       | CLEAR + @     | (ours)   |

Alternate combos (mnemonic: `[]` + SHIFT = `{}`):

| Character | Name           | Key Combo       |
|-----------|----------------|-----------------|
| `{`       | Left brace     | SHIFT+CLEAR + 8 |
| `}`       | Right brace    | SHIFT+CLEAR + 9 |
| `~`       | Tilde          | SHIFT+CLEAR + 7 |
| `\|`      | Pipe           | SHIFT+CLEAR + / |

## Screen Display

The CoCo 2 VDG (MC6847) has only one set of letter glyphs.
The console uses **normal video** for lowercase (common in Tcl) and
**inverse video** for uppercase (less common):

| ASCII Range      | Display        | Video Mode |
|------------------|----------------|------------|
| `a`–`z`          | Letter glyphs  | Normal     |
| `A`–`Z`          | Letter glyphs  | **Inverse**|
| Space – `?`      | Punctuation    | Normal     |
| `@` `[` `\` `]` `^` `_` | Symbols | Normal     |
| `` ` `` `{` `\|` `}` `~` | Symbols | **Inverse**|
| $80–$FF          | Semigraphics   | (as-is)    |

This means Tcl code (mostly lowercase letters, digits, and punctuation)
appears in calm green-on-black, while uppercase letters and the less-common
symbols stand out in inverse video.
