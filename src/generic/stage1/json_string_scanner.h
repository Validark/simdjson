#ifndef SIMDJSON_SRC_GENERIC_STAGE1_JSON_STRING_SCANNER_H

#ifndef SIMDJSON_CONDITIONAL_INCLUDE
#define SIMDJSON_SRC_GENERIC_STAGE1_JSON_STRING_SCANNER_H
#include <generic/stage1/base.h>
#include <generic/stage1/json_escape_scanner.h>
#endif // SIMDJSON_CONDITIONAL_INCLUDE

namespace simdjson {
namespace SIMDJSON_IMPLEMENTATION {
namespace {
namespace stage1 {

// Scans blocks for string characters, storing the state necessary to do so
class json_string_scanner {
public:
  simdjson_inline uint64_t next(uint64_t backslash, uint64_t raw_quote, uint64_t separated_values) noexcept;
  // Returns either UNCLOSED_STRING or SUCCESS
  simdjson_inline error_code finish() const noexcept;

private:
  simdjson_inline uint64_t next_unescaped_quotes(uint64_t backslash, uint64_t raw_quote) noexcept;
  simdjson_inline uint64_t next_in_string(uint64_t in_string, uint64_t separated_values) noexcept;

  // Scans for escape characters
  json_escape_scanner escape_scanner{};
  // Whether the last iteration was still inside a string (all 1's = true, all 0's = false).
  bitmask::borrow_t still_in_string = 0ULL;
};

//
// Return a mask of all string characters plus end quotes.
//
// prev_escaped is overflow saying whether the next character is escaped.
// prev_in_string is overflow saying whether we're still in a string.
//
// Backslash sequences outside of quotes will be detected in stage 2.
//
simdjson_inline uint64_t json_string_scanner::next(
  uint64_t backslash,                // 2+N
  uint64_t raw_quote,                // 2+N
  uint64_t separated_values          // 13
) noexcept {
  uint64_t quote = next_unescaped_quotes(backslash, raw_quote); // 3+N (2N+1+simd:2N total) or 7+N (2N+7+simd:2N total)
  uint64_t in_string = next_in_string(quote, separated_values); // 15 (+6) or (13+N or 17+N (+8+simd:3)).
  return in_string;
}

simdjson_inline uint64_t json_string_scanner::next_unescaped_quotes(
  uint64_t backslash, // 2+N
  uint64_t raw_quote  // 2+N
) noexcept {
  uint64_t escaped = escape_scanner.next(backslash).escaped; // 2+N (2 total) or 6+N (8 total)
  return raw_quote & ~escaped;                               // 3+N (3 total) or 7+N (9 total)
  // critical path: 3+N (3 total) or 7+N (9 total)
}

simdjson_inline uint64_t json_string_scanner::next_in_string(
  uint64_t quote,           // 3+N or 7+N
  uint64_t separated_values // 13
) noexcept {
  // Find values that are in the string. ASSUME that strings do not have separators/openers just
  // before the end of the string (i.e. "blah," or "blah,["). These are pretty rare.
  // TODO: we can also assume the carry in is 1 if the first quote is a trailing quote.
  uint64_t lead_quote     = quote &  separated_values;                 // 14 (+1)
  uint64_t trailing_quote = quote & ~separated_values;                 // 14 (+1)
  // If we were correct, the subtraction will leave us with:
  // LEAD-QUOTE=1 NON-QUOTE=1* TRAIL-QUOTE=0 NON-QUOTE=0* ...
  // The general form is this:
  // LEAD-QUOTE=1 NON-QUOTE=1|LEAD-QUOTE=0* TRAIL-QUOTE=0 NON-QUOTE=0|TRAIL-QUOTE=1* ...
  //                                                                   // 15 (+2)
  auto was_still_in_string = this->still_in_string;
  uint64_t in_string = bitmask::subtract_borrow(trailing_quote, lead_quote, this->still_in_string);
  // Assumption check! LEAD-QUOTE=0 means a lead quote was inside a string--meaning the second
  // quote was preceded by a separator/open.
  uint64_t lead_quote_in_string = lead_quote & ~in_string;             // 16 (+2)
  if (!lead_quote_in_string) {
    // This shouldn't happen often, so we take the heavy branch penalty for it and use the
    // high-latency prefix_xor.
    this->still_in_string = was_still_in_string;
    in_string = bitmask::prefix_xor(quote ^ this->still_in_string); // 13+N (+1+simd:3)
    this->still_in_string = in_string >> 63;                        // 14+N (+1)
  }
  return in_string;
  // critical path = 15 (+6) or (13+N or 17+N (+8+simd:3)).
  // would be 13+N or 17+N (+2+simd:3) by itself
}


simdjson_inline error_code json_string_scanner::finish() const noexcept {
  if (still_in_string) {
    return UNCLOSED_STRING;
  }
  return SUCCESS;
}

} // namespace stage1
} // unnamed namespace
} // namespace SIMDJSON_IMPLEMENTATION
} // namespace simdjson

#endif // SIMDJSON_SRC_GENERIC_STAGE1_JSON_STRING_SCANNER_H