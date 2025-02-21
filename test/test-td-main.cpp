/* 
    This file is part of TON Blockchain source code.

    TON Blockchain is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    TON Blockchain is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with TON Blockchain.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2017-2019 Telegram Systems LLP
*/
#include "td/utils/tests.h"
#include "td/utils/misc.h"

#include <cstring>

#include "td/utils/port/signals.h"

int main(int argc, char **argv) {
  td::set_default_failure_signal_handler().ensure();
  // TODO port OptionsParser to Windows
  auto &runner = td::TestsRunner::get_default();
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--filter")) {
      CHECK(i + 1 < argc);
      runner.add_substr_filter(argv[++i]);
    } else if (!std::strcmp(argv[i], "--stress")) {
      runner.set_stress_flag(true);
    } else if (!std::strcmp(argv[i], "--regression")) {
      CHECK(i + 1 < argc);
      runner.set_regression_tester(td::RegressionTester::create(argv[++i]));
    } else if (!std::strcmp(argv[i], "--verbosity")) {
      CHECK(i + 1 < argc);
      SET_VERBOSITY_LEVEL(td::to_integer<td::int32>(td::Slice(argv[++i])));
    }
  }
  runner.run_all();
  return 0;
}
