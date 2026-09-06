#pragma once
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cgtest {
struct Test {
  std::string name;
  std::function<void()> fn;
  bool enabled = true;
};
inline std::vector<Test>& registry() { static std::vector<Test> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
inline void reg(std::string name, std::function<void()> fn) { registry().push_back(Test{std::move(name), std::move(fn), true}); }
inline void fail(const std::string& msg) { ++failures(); std::cerr << "  FAIL: " << msg << "\n"; }
inline void pass_check() { ++checks(); }

struct Runner {
  int run() {
    std::cout << "Running " << registry().size() << " test case(s)\n";
    for (auto& t : registry()) {
      if (!t.enabled) continue;
      std::cout << "[RUN] " << t.name << "\n";
      try { t.fn(); }
      catch (const std::exception& e) { fail(t.name + ": " + e.what()); }
      catch (...) { fail(t.name + ": unknown exception"); }
    }
    std::cout << "Checks passed: " << checks() << "; Failures: " << failures() << "\n";
    return failures() == 0 ? 0 : 1;
  }
};
}  // namespace cgtest

#define CG_CHECK(cond)   do { ++cgtest::checks();     if (!(cond)) { std::ostringstream _cg_os_; _cg_os_ << __FILE__ << ":" << __LINE__       << " | CHECK failed: " << #cond; throw std::runtime_error(_cg_os_.str()); } } while(0)
#define CG_CHECK_EQ(a, b)   do { ++cgtest::checks();     auto _cg_a = (a); auto _cg_b = (b);     if (!(_cg_a == _cg_b)) { std::ostringstream _cg_os_; _cg_os_ << __FILE__ << ":" << __LINE__       << " | CHECK_EQ failed: " << #a << " == " << #b; throw std::runtime_error(_cg_os_.str()); } } while(0)
#define CG_CHECK_THROWS(expr)   do { ++cgtest::checks(); bool _cg_threw = false;     try { (void)(expr); } catch (...) { _cg_threw = true; }     if (!_cg_threw) { std::ostringstream _cg_os_; _cg_os_ << __FILE__ << ":" << __LINE__       << " | CHECK_THROWS failed (no exception): " << #expr; throw std::runtime_error(_cg_os_.str()); } } while(0)

#define CG_TEST_CASE(name)   static void cgtest_##name();   struct cgtest_reg_##name { cgtest_reg_##name() { cgtest::reg(#name, &cgtest_##name); } };   static cgtest_reg_##name cgtest_reginst_##name;   static void cgtest_##name()

#define CG_MAIN() int main() { return cgtest::Runner().run(); }
