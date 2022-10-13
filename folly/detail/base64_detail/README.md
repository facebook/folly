*This file contains explanation of how SIMD implementation of Base64 functions*

Our solution is based on 0x80 blog: [encoding](http://0x80.pl/notesen/2016-01-12-sse-base64-encoding.html), [decoding](http://0x80.pl/notesen/2016-01-17-sse-base64-decoding.html).

This is a different version of the same explanation + we have some differences.

*NOTE: We are using 1 digit for two bits when writing data*

# Understanding Base64 encoding.

*If you find it easier, [Wikipedia](https://en.wikipedia.org/wiki/Base64).*

The goal of base64 is to encode arbitrary byte data as text that can be passed around.
It works as follows:
* we select a 64 character alphabet.
* we insert two 0 bits after each 6 bits. 2^6 is 64.
* because we now only need to encode 64 values, our alphabet is sufficient.

# Encoding

So, we have 2 steps needed to do the conversion: 1. get indexes 0-64, 2. lookup the corresponding char.
There is also a little bit of tail handling, see the apropriate section.

If one letter represents two bits, it looks smth like this (remember - our machines are little endian):

```
`cddd`bbcc`aaab => 0ddd`0ccc`0bbb`0aaa
```

After this we just have to convert the bytes into the selected alphabet.

This means we have two steps to the algorithm: encodeToIndexes and lookupByIndex.

## encodeToIndexes

*NOTE: assume 16 byte registers.*

We will only be converting 12 bytes to output 16 bytes.
The last 4 loaded bytes are ignored.

1. We need to shuffle bytes in a way that each dword contains all the bits to construct output for that dword. Specifically:

```
PONM,LKJI,HGFE,DCBA => KLJK,GIGH,EFDE,BCAB
```

> STEP BY STEP:
> * We had bytes in memory ABCD.
> * Loaded on a LE machine they become DCBA.
> * The first output dword will only fit ABC,
    D goes into the second dword.
> * We will also reshuffle CBA as BCAB becuase this will make
    futher operations simpler.

After this we will mutate each dword (letter+digit indicate 2 bits):

```
shuffled b2b3c1c2'c3d1d2d3'a1a2a3b1'b2b3c1c2
exepcted 00d1d2d3'00c1c2c3'00b1b2b3'00a1a2a3
```

At the moment of writing this we only have an SSE4.2 version. We can do it fairly straightforwardly with shifts and blends. However the 0x80 blogs suggests that using a tricky mutliplication scheme is superior, and that's what we do.

For neon there are by element shift instructions which would help.
On avx2 there are `srlv` family of instructions that can come in handy as well.
The 0x80 blog also talks about a BMI implementation (pdep/pext instructions). They had issues on AMD but apparently not on the AMDs in Meta's fleet, so in the future we can consider it as a possibility as well.

## lookupByIndex

The idea is to use table lookup/shuffle instructions (`pshuvb` on sse4.2). They allow you to shuffle a register by index (`0..16`).

We have values `0..64`. However, we can utilize that the output values are not random. The are split into a few contigious groups. We don't have to lookup the complete encoding, we just need to lookup the offset to add.


| Index | Maps to | Add to encode |
| ----- | --------| ------------- |
| 0-25  | 'A'-'Z' | 'A' - 0       |
| 26-51 | 'a'-'z' | 'a' - 26      |
| 52-61 | '0'-'9' | '0' - 52      |
| 62    | '+'     | '+' - 62      |
| 63    | '/'     | '/' - 63      |


*NOTE: the URL version of Base64 encoding uses -_ instead of +/*.

Since we have 16 values to map to, we are not going to bother with consolidating 52-61 to a single base and instead will them up individually.

This is our final lookup table:

```
0: 'A' - 0
1: 'a' - 26
2: '0' - 52,
3: '1' - 53
...
11: '9' - 61
12: '+' - 62
13: '/' - 63
14: -
15: -
```

We even have two extra indexes `14` and `15` that are unused.

## Tail handling

What if we have less than register size to load?
What if we have less than 3 elements to encode?

Base64 for incomplete 3 bytes works as if there were
extra 0s at the end, except the garbage elements are replaced
with either a padding character or just removed.

Example:

|input bytes (be) | Basic  | Url  |
|-----------------|--------|------|
| (1)             |  AQ==  | AQ   |
| (1, 0)          |  AQA=  | AQA  |
| (1, 0, 0)       |  AQAA  | AQAA |

After measuring different options, using scalar loop peeling
proved to be the fastest.

# Decoding

Overall the decoding process is similar to inverse of the encoding: 1. from chars get numbers `0..63` 2. pack them
into bytes.

However, unlike encoding, not all the sequences of bytes are valid.
We have to decide which ones to accept and how to handle errors.

*NOTE: error detection is fairly expensive, if in the
future there is a need for a decode that can sometimes
swallow invalid values, it can be done faster*.

Decisions:
`base64Decode` requires `=` padding and accepts only [`+`, `/`] as 62/63 character.
If there are extra bits on the end, those are not allowed: iZ== is not a valid input
because no input sequence will produce it.
`base64URLDecode`: allows everything encoded with plain base64 and base64URL.
It will succesfully decode some combinations that are not either. The extra bits on
the end are currently allowed: iZ== will decode successfully (this decision is not
motivated by anything).

*NOTE:* details on `baseURLDecode` rules:
* Padding bytes follow regual base64: if they are present they are allowed to be only in positions that
  regular base64 allows them to be in: 4th or 3rd and 4th in the 4 byte chunk. "aa==", "aaa=" - OK, "aa=", "==" - not OK. This decision follows the previous most common implementation of base64 in the codebase.
* Both ['+', '/'] and ['-', '_'] are allowed in any combinations. "+-" is legal for example.


*NOTE: this follows the previous design and not necessarily how other places define
base64URLDecode. The difference comes to decoding invalid sequence of padding characters.
Example: "ba=" we consider invalid while some other implmenetations might just strip '='. We
suspect it won't be an issue*.

This behaviour matches what the previous implementation did.

We only have a SIMD version of this algorithm for `base64Decode` because the URL decode has
very little use (so far at least).

The SIMD algorithm has 2 steps:
- `decodeCharToIndex`: convert to numbers from `0..63`
- `packIndexesToBytes`: packs `0..63` into the originally encoded bytes.

If we encounter an error, we are not going to stop processing data. We are just going to mark it and then check at the end.

## decodeCharToIndex

Convertions we want

| Char      | Char's Hex code | Res     | Res Hex     |
|-----------|-----------------|---------|-------------|
| '+'       |   0x2B          | 62      | 0x3E        |
| '/'       |   0x2F          | 63      | 0x3F        |
| '0' - '9' |   0x30 - 0x39   | 52 - 61 | 0x34 - 0x3D |
| 'A' - 'Z' |   0x41 - 0x5A   | 0  - 25 | 0x00 - 0x19 |
| 'a' - 'z' |   0x61 - 0x7A   | 26 - 51 | 0x1A - 0x33 |

### Idea

We have unique higher 4 bits (nibble) per consecutive group,
except for '+' and '/'.
We can find a unique offset with pshuvb by it.

### Deal with '+' and '/' nibbles

We need to move '+' and '/' into their own nibbles.
It's important that all previously invalid chars stay invalid.

The best way I see to do it is to substact 0x0f from everything less than or equal to +.
The reason to use 0x0f is that it's a constant we will need anyways ot get 4 higher nibbles.

However on intel there is no unsigned comparison, so all the negative values will be considered as <= '+'. To deal with it, we can use a signed comparison but subtract with satuation.

 * <= '+' gives -1 where '+'
 * and 0x0f
 * subs

Everything bigger than '+' will stay the same.
Everything smaller than '+' is invalid and will become even smaller.
It will saturate to -127 which is still ivalid, so we are OK.

 *NOTE: I was also thinking if this is possible to do with just one subtruction and no mask. Unfortunatly we also need to keep a separate nibble for '0' and '/' and '0' == '/' + 1, so that subtraction will guarantee put '/' and '0' on the same higher nibble.*

 *NOTE: If we drop the requirement on invalid elements, again - this is much easier*.

New conversion table:

| Char      | New Hex code  | Res     | Res Hex     |
|-----------|---------------|---------|-------------|
| '+'       |   0x1C        | 62      | 0x3E        |
| '/'       |   0x2F        | 63      | 0x3F        |
| '0' - '9' |   0x30 - 0x39 | 52 - 61 | 0x34 - 0x3D |
| 'A' - 'Z' |   0x41 - 0x5A | 0  - 25 | 0x00 - 0x19 |
| 'a' - 'z' |   0x61 - 0x7A | 26 - 51 | 0x1A - 0x33 |

### Detect invalid values.

The idea is that we have valid higher nibbles in range from `1..7`.
We can convert higher nibbles into a bitmask `0000'0010 ... 1000'0000` (`1 << nibble`)
Due to a lack of proper shift instruction on x86, we will use a table lookup (`pshuvb`).
All other higher nibbles can become 0, indicating invalid input.

Now for all possible lower nibbles we have a set of valid higher nibbles.


| Lower Nibble | Valid higher nibble |
| ------------ | ------------------- |
| 0            | 3, 5, 7             |
| 1 - 9        | 3, 4, 5, 6, 7       |
| A            | 4, 5, 6, 7          |
| B            | 4, 6                |
| C            | 1, 4, 6             |
| D, E         | 4, 6                |
| F            | 2, 4, 6             |


So: by higher nibble lookup a bit that indicates that higher nibble
    by lower nible lookup a set of valid bits
    and

This will give us a non-zero when it's OK.
And 0 when it's not.

Between iterations we can accumulate error with unsigned min.
0 - is the smallest value - so we will get 0 if there was one.

### Offset to correct values.

Similar idea to encode - we just lookup the proper offset and add it.
We'll need to lookup by higher nibble.

| Char      | New Hex code  | Res     | Offset (- means minus) |
|-----------|---------------|---------|------------------------|
| '+'       |   0x1C        | 62      | 62 - 0x1C              |
| '/'       |   0x2F        | 63      | 63 - '/'               |
| '0' - '9' |   0x30 - 0x39 | 52 - 61 | 52 - '0'               |
| 'A' - 'O' |   0x41 - 0x4F | 0  - 14 | 0  - 'A'               |
| 'P' - 'Z' |   0x50 - 0x5A | 15 - 25 | 0  - 'A'               |
| 'a' - 'o' |   0x61 - 0x6F | 26 - 40 | 26 - 'a'               |
| 'p' - 'z' |   0x70 - 0x7A | 26 - 51 | 26 - 'a'               |

## packIndexesToBytes

*NOTE: assume 16 byte registers.*

From a register of 16 bytes we need to get 12 bytes.
I think the only way to do it is to convert each 4 bytes to
appropriate 3 bytes and then put them all together.
After we doing conversion 4 to 3 we'll remove the leftover byte
with one shuffle.

Goal (*One letter - 2 bits*):

- Input: `0ddd'0ccc'0bbb'0aaa`
- Needed bytes: `'cddd'bbcc'aaab`

We can obviously shift and or to get the desired result
(and maybe on ARM we should) but again, shifting story for x86 is poor.

The 0x80 blog suggests the following trick.

### Multiply add (MADD) solution

On SSE there are `madd` instrucitons for adjacent bytes (`_mm_maddubs_epi16`)  and words (`_mm_madd_epi16`). Multiply is a strictly more powerful operation than shift, so this will work.

*NOTE: all of our numbers are positive so the sign of the operations doesn't matter*

Here is the scalar equivalent of the code we'll do.
(*I omit indicating leading 0s and ignore casts*).

```
std::uint16_t aaabbb = aaa << 6 + bbb;
std::uint16_t cccddd = ccc << 6 + ddd;
std::uint32_t aaabbbcccddd = aaabbb << 12 + cccddd;
```

Multiplication equivalent of `<< 6` is `* 0x40` and of `<< 12` is `* 0x1000`.

Now that we have `0000'aaab'bbcc'cddd`.
The correct order `cddd'bccc'aaab` which we can mix
into the final shuffle.

## looping

We can convert only in registers. There is a question: when can we write the whole
register to the output?
For example, for 16 bytes of input (no padding) we only have 12 bytes of output space.

A simple way to answer if we have enough space is to compute how much output space we'll need overall.
However, this operation is somewhat expensive and is likely already done for the allocation. So computing output size ideally should be avoided.

We can prove that as long as the `Register` is bigger than 16 bytes, `1.5 Register` of input space is enough to guarantee at least one register of the output sopace.

Proof.

Out of `1.5 Register`, `1 Register` converts to `0.75 Register` in the output. So we are left with proving that `0.5 Register` will produce at least `0.25 Register` of the output.

If there is no padding - `0.5` converts to `3/8`, which is `> 0.25`.
If there is padding, the last dword of input might produce just `1` output byte.

So, we need to prove that:

`convertedSize(RegisterSize / 2 - 4) >= RegisterSize / 4 - 1`

For `16` bytes it works `convertedSize(4) == 3`.
For `Register > 16` bytes, we can split:

```
convertedSize(RegisterSize / 4) +
convertedSize(RegisterSize / 4 - 4) >=
RegisterSize / 8 +
RegisterSize / 8 - 1
```

Since `convertedSize(RegisterSize / 4) > RegisterSize / 8` we just reduced the problem to 16 bytes.

Proven.

## Decoding SWAR

SWAR stands for SIMD Within A Register i.e. using bit tricks to
process multiple bytes in a bigger integer register.
Previous version of Base64Decode was using some bit tricks and, as a result,
was beating our performance for tails.

On top of that, if the platform lacks the necessary SIMD capabilities, SWAR is
benefitial, so we decided to implement it.

How does it work?

The simple version of the decoding alogirhtm matches from the input char to
the index: let's say char `a` is matched to the number `26`. So we would first match
chars to the corresponding indexes and then blend those indexes toghether.

Let's say (one letter still 2 bits):

```
0AAA'0BBB'0CCC'0DDD  // input chars
0aaa'0bbb'0ccc'0ddd  // correspoding bytes
cddd'bbcc'aaab'____  // after a bunch of bit manipulations
                     // (last byte is w/e)
```

However, what if we incorporated both position and character in the lookup table?

```
char:                 0AAA
corresponds to index: 0aaa

[0][0AAA] = 0000'0000'aaa0'0000
[1][0AAA] = 0000'aa00'000a'0000
[2][0AAA] = a000'00aa'0000'0000
[4][0AAA] = 0aaa'0000'0000'0000
```

This way we can just `or` results for all input chars.
Since we have one byte, which is always `0000`, we can use that byte for marking errors.
So for a busted input we can just store `0xffffffff`. We will `or` everything we output - and then check for `0xffffffff` in the end.

This is the formula for each position (d - decoded index for char):

```
d << 2;                                   // 0: 0000'0000'0000'aaa0
d >> 4 | (d << 12 & 0xff00);              // 1: 0000'0000'bb00'000b
(d << 6 & 0xff00) | (d << 22 & 0xff0000); // 2: 0000'c000'00cc'0000
d << 16;                                  // 3: 0000'0ddd'0000'0000
```

This is a memory/speed trade off. For a simple table lookup decoding we store 256 bytes. For this decoding we store 256 * 4 * 4 = 4kb. On big enough data this is a win of about a third over naive code. As far as SIMD clean up code is concerned, this is more questionable but for now we decided to go with it given that it shows an
improvement in microbenchmarks.
