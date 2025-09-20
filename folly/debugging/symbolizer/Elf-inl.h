/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

namespace folly {
namespace symbolizer {

template <class Fn>
const ElfPhdr* ElfFile::iterateProgramHeaders(Fn fn) const
    noexcept(is_nothrow_invocable_v<Fn, ElfPhdr const&>) {
  // there exist ELF binaries which execute correctly, but have invalid internal
  // offset(s) to program/section headers; most probably due to invalid
  // stripping of symbols
  if (elfHeader().e_phoff + sizeof(ElfPhdr) >= length_) {
    return nullptr;
  }

  const ElfPhdr* ptr = &at<ElfPhdr>(elfHeader().e_phoff);
  for (size_t i = 0; i < elfHeader().e_phnum; i++, ptr++) {
    if (fn(*ptr)) {
      return ptr;
    }
  }
  return nullptr;
}

template <class Fn>
const ElfShdr* ElfFile::iterateSections(Fn fn) const
    noexcept(is_nothrow_invocable_v<Fn, ElfShdr const&>) {
  // there exist ELF binaries which execute correctly, but have invalid internal
  // offset(s) to program/section headers; most probably due to invalid
  // stripping of symbols
  if (elfHeader().e_shoff + sizeof(ElfShdr) >= length_) {
    return nullptr;
  }

  const ElfShdr* ptr = &at<ElfShdr>(elfHeader().e_shoff);
  for (size_t i = 0; i < elfHeader().e_shnum; i++, ptr++) {
    if (fn(*ptr)) {
      return ptr;
    }
  }
  return nullptr;
}

template <class Fn>
const ElfShdr* ElfFile::iterateSectionsWithType(uint32_t type, Fn fn) const
    noexcept(is_nothrow_invocable_v<Fn, ElfShdr const&>) {
  return iterateSections([&](const ElfShdr& sh) {
    return sh.sh_type == type && fn(sh);
  });
}

template <class Fn>
const ElfShdr* ElfFile::iterateSectionsWithTypes(
    std::initializer_list<uint32_t> types, Fn fn) const
    noexcept(is_nothrow_invocable_v<Fn, ElfShdr const&>) {
  return iterateSections([&](const ElfShdr& sh) {
    auto const it = std::find(types.begin(), types.end(), sh.sh_type);
    return it != types.end() && fn(sh);
  });
}

template <class Fn>
const char* ElfFile::iterateStrings(const ElfShdr& stringTable, Fn fn) const
    noexcept(is_nothrow_invocable_v<Fn, const char*>) {
  validateStringTable(stringTable);

  const char* start = file_ + stringTable.sh_offset;
  const char* end = start + stringTable.sh_size;

  const char* ptr = start;
  while (ptr != end && !fn(ptr)) {
    ptr += strlen(ptr) + 1;
  }

  return ptr != end ? ptr : nullptr;
}

template <typename E, class Fn>
const E* ElfFile::iterateSectionEntries(const ElfShdr& section, Fn&& fn) const

    noexcept(is_nothrow_invocable_v<E const&>) {
  FOLLY_SAFE_CHECK(
      section.sh_entsize == sizeof(E), "invalid entry size in table");

  const E* ent = &at<E>(section.sh_offset);
  const E* end = ent + (section.sh_size / section.sh_entsize);

  while (ent < end) {
    if (fn(*ent)) {
      return ent;
    }

    ++ent;
  }

  return nullptr;
}

template <class Fn>
const ElfSym* ElfFile::iterateSymbols(const ElfShdr& section, Fn fn) const
    noexcept(is_nothrow_invocable_v<Fn, ElfSym const&>) {
  return iterateSectionEntries<ElfSym>(section, fn);
}

template <class Fn>
const ElfSym* ElfFile::iterateSymbolsWithType(
    const ElfShdr& section, uint32_t type, Fn fn) const
    noexcept(is_nothrow_invocable_v<Fn, ElfSym const&>) {
  // N.B. st_info has the same representation on 32- and 64-bit platforms
  return iterateSymbols(section, [&](const ElfSym& sym) -> bool {
    return ELF32_ST_TYPE(sym.st_info) == type && fn(sym);
  });
}

template <class Fn>
const ElfSym* ElfFile::iterateSymbolsWithTypes(
    const ElfShdr& section, std::initializer_list<uint32_t> types, Fn fn) const
    noexcept(is_nothrow_invocable_v<Fn, ElfSym const&>) {
  // N.B. st_info has the same representation on 32- and 64-bit platforms
  return iterateSymbols(section, [&](const ElfSym& sym) -> bool {
    auto const elfType = ELF32_ST_TYPE(sym.st_info);
    auto const it = std::find(types.begin(), types.end(), elfType);
    return it != types.end() && fn(sym);
  });
}

template <class Fn>
folly::Expected<ElfFile::Note, ElfFile::FindNoteError>
ElfFile::iterateNotesInBodyHelper(folly::StringPiece body, Fn& fn) const
    noexcept(is_nothrow_invocable_v<Fn, const Note&>) {
  static_assert(alignof(ElfNhdr) >= 4);
  if (uintptr_t(body.data()) % alignof(ElfNhdr) != 0) {
    return Unexpected(FindNoteError(FindNoteFailureCode::NoteUnaligned));
  }

  while (body.size() > 0) {
    folly::span<const uint8_t> noteBody =
        span(reinterpret_cast<const uint8_t*>(body.data()), body.size());
    auto noteMaybe = Note::parse(noteBody);
    if (!noteMaybe) {
      return noteMaybe;
    }

    auto note = *noteMaybe;

    if (fn(note)) {
      return note;
    }

    body.advance(note.alignedSize());
  }

  return Unexpected(FindNoteError(FindNoteFailureCode::NoteNotFound));
}

template <class Fn>
folly::Expected<ElfFile::Note, ElfFile::FindNoteError>
ElfFile::iterateNotesInSections(const ElfShdr* section, Fn fn) const
    noexcept(is_nothrow_invocable_v<Fn, const Note&>) {
  if (section != nullptr) {
    return iterateNotesInBodyHelper(getSectionBody(*section), fn);
  }

  folly::Expected<Note, FindNoteError> noteMaybe =
      Unexpected(FindNoteError(FindNoteFailureCode::NoteNotFound));
  iterateSectionsWithType(SHT_NOTE, [&](const ElfShdr& sh) {
    noteMaybe = iterateNotesInBodyHelper(getSectionBody(sh), fn);
    // Check if we got a good result, if so return it
    if (noteMaybe) {
      return STOP;
    }

    // Check if we got a data corruption error, stop and return that.
    if (noteMaybe.error().isDataCorruptionError()) {
      return STOP;
    }
    return CONTINUE;
  });

  return noteMaybe;
}

template <class Fn>
folly::Expected<ElfFile::Note, ElfFile::FindNoteError>
ElfFile::iterateNotesInSegments(const ElfPhdr* segment, Fn fn) const
    noexcept(is_nothrow_invocable_v<Fn, const Note&>) {
  if (segment != nullptr) {
    return iterateNotesInBodyHelper(getSegmentBody(*segment), fn);
  }

  folly::Expected<Note, FindNoteError> noteMaybe =
      Unexpected(FindNoteError(FindNoteFailureCode::NoteNotFound));
  iterateProgramHeaders([&](const ElfPhdr& ph) {
    if (ph.p_type != PT_NOTE) {
      return CONTINUE;
    }

    noteMaybe = iterateNotesInBodyHelper(getSegmentBody(ph), fn);
    // Check if we got a good result, if so return it
    if (noteMaybe) {
      return STOP;
    }

    // Check if we got a data corruption error, stop and return that.
    if (noteMaybe.error().isDataCorruptionError()) {
      return STOP;
    }
    return CONTINUE;
  });

  return noteMaybe;
}

} // namespace symbolizer
} // namespace folly
