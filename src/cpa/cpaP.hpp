/* Copyright (c) 2013 Tescase
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

//
// This file contains the serial and parallel
// CPA algorithms
//

#ifndef SCA_CPAP_CPAP_H
#define SCA_CPAP_CPAP_H

#include <string>
#include "cpa.hpp"

namespace cpaP
{

// The serial CPU based CPA function
void cpaP(std::string data_path, std::string ct_path, std::string power_model_path, std::string cells_type_path, bool clk_high, std::string key_path, std::string perm_path, bool HW, bool HD, bool SNR_flag, bool candidates, int permutations, int steps, int steps_start, int steps_stop, float rate_stop, int verbose, bool key_expansion);


} //end namespace

#endif
