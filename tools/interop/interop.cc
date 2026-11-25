// Exchanges a buffer between a 32bit and a 64bit build:
//   cista-interop write <file>   serializes the payload to <file>
//   cista-interop read <file>    deserializes <file> and checks every field
// Both builds need CISTA_64BIT_OFFSET.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string_view>

#include "cista/containers/cstring.h"
#include "cista/containers/hash_map.h"
#include "cista/containers/hash_set.h"
#include "cista/containers/optional.h"
#include "cista/containers/pair.h"
#include "cista/containers/string.h"
#include "cista/containers/tuple.h"
#include "cista/containers/unique_ptr.h"
#include "cista/containers/variant.h"
#include "cista/containers/vector.h"
#include "cista/serialization.h"

static_assert(sizeof(cista::offset_t) == 8U, "build with CISTA_64BIT_OFFSET");

namespace data = cista::offset;

constexpr auto const kMode = cista::mode::WITH_INTEGRITY;

struct inner {
  std::int32_t a_;
  data::string s_;
};

struct payload {
  std::uint32_t magic_;
  data::vector<std::int64_t> ints_;
  data::string short_str_;
  data::string long_str_;
  data::indexed_vector<inner> inners_;
  data::hash_map<data::string, std::int32_t> map_;
  data::hash_set<std::int64_t> set_;
  data::unique_ptr<inner> unique_;
  cista::optional<std::int16_t> opt_;
  data::variant<std::int32_t, data::string> variant_;
  data::vector<data::vector<std::uint8_t>> nested_;
  data::ptr<inner> ptr_;
  data::cstring cstring_;
  // 3 bytes: the next buffer write is misaligned unless doubles are 8 byte
  // aligned on every platform.
  data::vector<std::uint8_t> odd_;
  data::vector<double> doubles_;
  cista::optional<std::int64_t> opt64_;
  data::variant<std::uint8_t, std::int64_t> variant64_;
  data::vector<cista::pair<std::int32_t, std::int64_t>> pairs_;
  cista::tuple<std::uint8_t, std::int64_t, double> tuple_;
  std::uint8_t flag_;
  std::int64_t last_;
};

constexpr auto const kLongStr =
    "a string that is too long for the short string optimization";

payload make_payload() {
  auto p = payload{};
  p.magic_ = 0xC157A001U;
  for (auto i = std::int64_t{0}; i != 100; ++i) {
    p.ints_.push_back(i * 1'000'000'007LL - 50);
  }
  p.short_str_ = "short";
  p.long_str_ = kLongStr;
  for (auto i = 0; i != 5; ++i) {
    p.inners_.push_back(inner{i, data::string{std::to_string(i * 11)}});
  }
  for (auto i = 0; i != 50; ++i) {
    p.map_.emplace(data::string{"key " + std::to_string(i)}, i * 3);
    p.set_.emplace(std::int64_t{i} << 40U);
  }
  p.unique_ = data::make_unique<inner>(inner{42, data::string{kLongStr}});
  p.opt_ = std::int16_t{-1234};
  p.variant_ = data::string{"variant string, long enough to be on the heap"};
  p.nested_.emplace_back(data::vector<std::uint8_t>{1U, 2U, 3U});
  p.nested_.emplace_back();
  p.nested_.emplace_back(data::vector<std::uint8_t>{255U});
  p.ptr_ = &p.inners_[3];
  p.cstring_ = kLongStr;
  p.odd_ = {1U, 2U, 3U};
  p.doubles_ = {0.5, -1.25, 1e300};
  p.opt64_ = std::int64_t{-5'000'000'000LL};
  p.variant64_ = std::int64_t{6'000'000'000LL};
  p.pairs_.push_back(cista::pair<std::int32_t, std::int64_t>{1, 1LL << 40U});
  p.pairs_.push_back(
      cista::pair<std::int32_t, std::int64_t>{-2, -(1LL << 41U)});
  p.tuple_ = cista::tuple<std::uint8_t, std::int64_t, double>{
      std::uint8_t{9U}, std::int64_t{7'000'000'000LL}, 2.5};
  p.flag_ = 7U;
  p.last_ = -9'000'000'000'000'000'000LL;
  return p;
}

int g_errors = 0;

void check(bool const ok, char const* what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++g_errors;
  }
}

void check_equal(payload const& a, payload const& e) {
  check(a.magic_ == e.magic_, "magic");
  check(a.ints_ == e.ints_, "ints");
  check(a.short_str_ == e.short_str_, "short string");
  check(a.long_str_ == e.long_str_, "long string");
  check(a.inners_.size() == e.inners_.size(), "inners size");
  for (auto i = 0U; i != a.inners_.size() && i != e.inners_.size(); ++i) {
    check(a.inners_[i].a_ == e.inners_[i].a_ &&
              a.inners_[i].s_ == e.inners_[i].s_,
          "inners");
  }
  check(a.map_.size() == e.map_.size(), "map size");
  for (auto const& [k, v] : e.map_) {
    auto const it = a.map_.find(k);
    check(it != a.map_.end() && it->second == v, "map entry");
  }
  check(a.set_.size() == e.set_.size(), "set size");
  for (auto const& x : e.set_) {
    check(a.set_.find(x) != a.set_.end(), "set entry");
  }
  check(a.unique_ != nullptr && a.unique_->a_ == e.unique_->a_ &&
            a.unique_->s_ == e.unique_->s_,
        "unique_ptr");
  check(a.opt_.has_value() && *a.opt_ == *e.opt_, "optional");
  check(a.variant_.index() == 1U && cista::get<data::string>(a.variant_) ==
                                        cista::get<data::string>(e.variant_),
        "variant");
  check(a.nested_ == e.nested_, "nested vectors");
  check(a.ptr_ == &a.inners_[3], "pointer into vector");
  check(a.cstring_ == e.cstring_, "cstring");
  check(a.odd_ == e.odd_, "odd bytes");
  check(a.doubles_ == e.doubles_, "doubles");
  check(a.opt64_.has_value() && *a.opt64_ == *e.opt64_, "optional<int64_t>");
  check(
      a.variant64_.index() == 1U && cista::get<std::int64_t>(a.variant64_) ==
                                        cista::get<std::int64_t>(e.variant64_),
      "variant<uint8_t, int64_t>");
  check(a.pairs_.size() == e.pairs_.size(), "pairs size");
  for (auto i = 0U; i != a.pairs_.size() && i != e.pairs_.size(); ++i) {
    check(a.pairs_[i].first == e.pairs_[i].first &&
              a.pairs_[i].second == e.pairs_[i].second,
          "pairs");
  }
  check(cista::get<0>(a.tuple_) == cista::get<0>(e.tuple_) &&
            cista::get<1>(a.tuple_) == cista::get<1>(e.tuple_) &&
            cista::get<2>(a.tuple_) == cista::get<2>(e.tuple_),
        "tuple");
  check(a.flag_ == e.flag_, "flag");
  check(a.last_ == e.last_, "last");
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s write|read <file>\n", argv[0]);
    return 2;
  }

  auto const cmd = std::string_view{argv[1]};
  auto const arch = sizeof(void*) * 8U;
  if (cmd == "write") {
    auto const p = make_payload();
    auto const buf = cista::serialize<kMode>(p);
    std::ofstream{argv[2], std::ios::binary}.write(
        reinterpret_cast<char const*>(buf.data()),
        static_cast<std::streamsize>(buf.size()));
    std::printf("%zubit: wrote %zu bytes to %s\n", arch, buf.size(), argv[2]);
    return 0;
  } else if (cmd == "read") {
    auto in = std::ifstream{argv[2], std::ios::binary};
    auto buf = cista::byte_buf{std::istreambuf_iterator<char>{in},
                               std::istreambuf_iterator<char>{}};
    auto const* p = cista::deserialize<payload, kMode>(buf);
    check_equal(*p, make_payload());
    std::printf("%zubit: read %zu bytes from %s: %s\n", arch, buf.size(),
                argv[2], g_errors == 0 ? "OK" : "FAILED");
    return g_errors == 0 ? 0 : 1;
  }

  std::fprintf(stderr, "unknown command %s\n", argv[1]);
  return 2;
}
