<!--
SPDX-FileCopyrightText: 2026 SombrAbsol

SPDX-License-Identifier: MIT
-->

# MES File Format
Used to store texts in:
* *Pokémon Ranger: Shadows of Almia* (version 1)
* *Pokémon Ranger: Guardian Signs* (version 2)

## Format specifications
### Overview
* 32-bit little-endian integer values
* Encoded as raw byte strings
* Always null-terminated
* Followed by zero padding so that the total size is a multiple of 4 bytes

### Pokémon Ranger: Shadows of Almia
This game uses version 1 of the MES format, where strings are stored sequentially, each preceded by a block size. They are read in order; offsets are implicit.
```rust
{
  u32 total_size // total file size in bytes
  u32 count      // number of strings
}
```

Repeat count times:
```rust
{
  u32  block_size // string length + null terminator + padding bytes
  char string[]   // null-terminated
  // zero padding bytes
}
```

### Pokémon Ranger: Guardian Signs
This game uses version 2 of the MES format, where strings are stored in a data section and referenced sequentially via an offset table.
```rust
{
  u32 total_size // total file size in bytes
  u32 count      // number of strings
}
```

Offset table begins at byte `0x08` and count entries. Offsets are absolute, relative to the start of the file.
```rust
{
  u32 offset[0]
  u32 offset[1]
  // …
  u32 offset[n-1]
}
```

String data, repeated for each string:
```rust
{
  char string[] // null-terminated
  // zero padding bytes
}
```

## List of control characters
Unless constants are specified, X is a numeric variable, whose action associated with its value (usually 0, then 1, 2, etc. if necessary) is assigned on the fly in the game code. For instance, if the game decides to set `[P:0]` to Pikachu, then using `[P:0]` in the text displayed at that moment will display the name "Pikachu". Another instance: the text color is a constant, so using `[C:5]` in the text will always output green text.

### Pokémon Ranger: Shadows of Almia
* `[B]` Big text
* `[C:X]` Text color
  * `0` Transparent
  * `1` White
  * `2` Black (default)
  * `3` Red
  * `4` Blue
  * `5` Green
* `[E]` Line break
* `[F:X]` Move name
* `[K:X]` Number
* `[M]` Player name
* `[N:X]` Pokémon encounter text
  * `0` Empty
  * `1` "The wild" (default)
  * `2` "attacked!"
* `[O:XX]` Display image 
  * `04` Styler Menu button icon
  * `05` Change Screen button icon
* `[P:X]` Pokémon name
* `[Q]` Yes/No question
* `[R]` New dialog page after button press
* `[V:X]` Play Pokémon cry
* `[W:XX]` Text speed
  * `00` Default
  * `01` Fast
  * `03` A bit slow
  * Slower as the value increases
  * `60` Slowest speed used in-game
* `[Y:X]` Target name

### Pokémon Ranger: Guardian Signs
* `[C:X]` Text color
  * `0` White (default)
  * `1` Orange in Ranger Net menu, white elsewhere
  * `2` Black
  * `3` Red
  * `4` Blue
  * `5` Green
* `[D]` Resets text speed to default, remove first character on the right
* `[D:]` Resets text speed to default
* `[E]` Line break
* `[F:X]` Destination name
* `[K:X]` Number
* `[M]` Player name, remove first character on the right
* `[M:]` Player name
* `[N:X]` Player's counterpart name
* `[O:XX]` Display image 
  * `00` Speech bubble with ellipsis
  * `01` Cut Field Move icon
  * `02` Recharge Field Move icon
  * `03` Fire Group icon
  * `04` Water Group icon
  * `05` Grass Group icon
  * `06` Electric Group icon
  * `07` Ground Group icon
* `[P:X]` Pokémon name
* `[Q]` Yes/No question (auto skip text if no answer is defined)
* `[Q:]` Yes/No question (auto skip text if no answer is defined), remove first character on the right
* `[R]` New dialog page after button press
* `[S:XX]` Text speed, testing only and non-functional
* `[T:X]` Target name
* `[W:XX]` Text speed
  * `00` Instant
  * `01` Fast
  * `02` Normal (default)
  * Slower as the value increases
  * `90` Slowest speed used in-game
