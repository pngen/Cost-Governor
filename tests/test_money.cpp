#include "test_helpers.hpp"
#include "costgovernor/money.hpp"

using namespace costgovernor;

CG_TEST_CASE(Money_exact_add_subtract) {
  MoneyMicros a = MoneyMicros::from_units(100);   // 100.00
  MoneyMicros b = MoneyMicros::from_units(25);
  CG_CHECK((a + b) == MoneyMicros::from_units(125));
  CG_CHECK((a - b) == MoneyMicros::from_units(75));
  CG_CHECK(MoneyMicros::from_micros(1) + MoneyMicros::from_micros(2) == MoneyMicros::from_micros(3));
  // no float drift
  MoneyMicros x = MoneyMicros::from_micros(1);
  for (int i = 0; i < 100000; ++i) x += MoneyMicros::from_micros(1);
  CG_CHECK(x == MoneyMicros::from_micros(100001));
}

CG_TEST_CASE(Money_overflow_rejected) {
  MoneyMicros maxv = MoneyMicros::from_micros(std::numeric_limits<std::int64_t>::max());
  MoneyMicros one = MoneyMicros::from_micros(1);
  CG_CHECK_THROWS(maxv + one);
  MoneyMicros minv = MoneyMicros::from_micros(std::numeric_limits<std::int64_t>::min());
  CG_CHECK_THROWS(minv - MoneyMicros::from_micros(1));
  CG_CHECK_THROWS(maxv.checked_mul(2));
}

CG_TEST_CASE(Money_checked_mul_div) {
  MoneyMicros a = MoneyMicros::from_units(10);
  CG_CHECK(a.checked_mul(3) == MoneyMicros::from_units(30));
  CG_CHECK(a.checked_div(3, MoneyMicros::Rounding::kTruncate).total_micros() == 3333333);
  // 10.000000 / 3 = 3.333333... ceil -> 3333334 micros
  MoneyMicros ten = MoneyMicros::from_units(10);
  MoneyMicros q = ten.checked_div(3, MoneyMicros::Rounding::kCeil);
  CG_CHECK(q.total_micros() == 3333334);
  MoneyMicros qf = ten.checked_div(3, MoneyMicros::Rounding::kFloor);
  CG_CHECK(qf.total_micros() == 3333333);
  CG_CHECK_THROWS(ten.checked_div(0));
}

CG_TEST_CASE(Money_decimal_serialization) {
  CG_CHECK(MoneyMicros::from_units(123).to_string() == "123.000000");
  CG_CHECK(MoneyMicros::from_micros(1000000).to_string() == "1.000000");
  CG_CHECK(MoneyMicros::from_micros(-1000000).to_string() == "-1.000000");
  CG_CHECK(MoneyMicros::from_micros(-1).to_string() == "-0.000001");
  MoneyMicros minv = MoneyMicros::from_micros(std::numeric_limits<std::int64_t>::min());
  CG_CHECK(minv.to_string().size() > 5);  // must not wrap/UB
}
