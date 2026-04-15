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
// This file process command line arguments and launches the Side
// Channel Algorithms
//

#include <iostream>
#include <string>
#include <cstring>
#include <chrono>
#include <omp.h>

#include "common/aes-op.hpp"
#include "cpa/cpa.hpp"
#include "cpa/cpaP.hpp"

int main(int argc, char *argv[])
{
	//	
	// Create error messages
	//
	// -----------------------------------------------------------------------------------
	const char* input_msg = "\nIn order to use the SCA Tool, please provide a power traces data\n"
			          "file and a cipher texts file (in hexadecimal). Use the --help\n"
			          "flag for more information.\n\n";
	
	const char* help_msg = "\nOptions:\n\n"
				"-d           Mandatory: power traces file\n"
				"-t           Mandatory: cipher texts file\n"
				"-HD          Optional: use Hamming distance model\n"
				"                if not provided, HW and power model are checked for\n"
				"-HW          Optional: use Hamming weight model\n"
				"                if not provided, power model is checked for\n"
				"-pm          Optional: power model file\n"
				"                if provided, requires -sc as well then\n"
				"                if not provided, fall-back default is HD model\n"
				"-sc          Optional: cells type file for all state flip-flops\n"
				"                if provided, requires -pm as well then\n"
				"                if not provided, fall-back default is HD model\n"
				"-clk         Optional: assume clock high/1 for power model look-up\n"
				"                if not provided, clock low/0 is assumed\n"
				"-candidates  Optional: print all key candidates, ordered by their correlation values,\n"
				"                with the highest-correlation candidate coming last\n"
				"-steps       Optional: specify the steps size, i.e., the total count of subsets of traces to consider;\n"
				"                if not provided, all the traces will be considered, but no subsets\n"
				"-steps_start Optional: specify the start step;\n"
				"                if not provided, start with step 1\n"
				"-steps_stop  Optional: specify the stop step;\n"
				"                if not provided, stop according to ``steps''\n"
				"-perm        Optional: specify the number of permutations to consider per subset of traces;\n"
				"                if not provided, only one permutation is considered per subset of traces\n"
				"-rate_stop   Optional: specify the success rate (e.g., 99.99) for when to stop exploration of subsets;\n"
				"                if not provided, stop according to ``steps_stop''\n"
				"-perm_file   Optional: the file with permutations indices to consider per subset of traces;\n"
				"                if not provided, random permutations are generated;\n"
				"                if provided, make sure to specify -steps as well (and optionally -steps_start -steps_top);\n"
				"                if not found, random permutations are generated and written out to the file;\n"
				"-k           Optional: correct key file\n"
				"                if not provided, the success rate across the subsets and permutations cannot be calculated\n"
				"-SNR         Optional: compute SNR (related to TVLA) for power traces\n"
				"-no_key_exp  Optional: don't apply key expansion, just use the provided key as correct round-10 key\n"
				//"-p           Optional: use parallel analysis (requires GPU)\n"
				"-threads     Optional: threads used for OpenMP (for correlation calculation)\n"
				"                if not provided, set to 16\n"
				"-nv          Optional: non-verbose logging\n"
				"-v           Optional: verbose logging\n"
				"\n\n";

	const char* wrong_input_msg = " is not recognized as an option\n\n";
	
	//#if !defined(OPENCL) && !defined(CUDA)
	//const char* no_gpu_found = "\nNo drivers have been found for an AMD or NVIDIA GPU.\n"
	//			   "Please install either OpenCL or CUDA to use this option.\n\n";
	//#endif	
	//// ------------------------------------------------------------------------------------
	

	//int parallel_analysis = 0;
	int data_path_set = 0;
	int ct_path_set = 0;
	int power_model_path_set = 0;
	int cells_type_path_set = 0;

	bool clk_high = false; // false means clk low
	bool candidates = false;
	bool key_expansion = true;
	bool HW = false;
	bool HD = false;
	bool SNR = false;

	// -1 indicates non-verbose, 0 indicates regular, and 1 indicates verbose
	int verbose = 0;

	// -1 indicates that these parameters have not been set yet
	int permutations = -1;
	int steps = -1;
	int steps_start = -1;
	int steps_stop = -1;
	int threads = -1;
	// -1 here means that it should not be used
	float rate_stop = -1;
	
	std::string data_path;
	std::string ct_path;
	std::string power_model_path;
	std::string cells_type_path;
	std::string key_path = "";
	std::string perm_path = "";

	// Check number of arguments
	if (argc == 1)
	{
		std::cerr<<input_msg;
		return 1;
	}

	// Check command line arguments
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--help"))
		{
			std::cout<<help_msg;
			return 0;
		}
		else if (!strcmp(argv[i], "-pm"))
		{
			power_model_path = argv[i + 1];
			power_model_path_set = 1;
			i++;
		}
		else if (!strcmp(argv[i], "-sc"))
		{
			cells_type_path = argv[i + 1];
			cells_type_path_set = 1;
			i++;
		}
		else if (!strcmp(argv[i], "-d"))
		{
			data_path = argv[i + 1];
			data_path_set = 1;
			i++;
		}
		else if (!strcmp(argv[i], "-t"))
		{
			ct_path = argv[i + 1];
			ct_path_set = 1;
			i++;
		}
		else if (!strcmp(argv[i], "-k"))
		{
			key_path = argv[i + 1];
			i++;
		}
		else if (!strcmp(argv[i], "-perm_file"))
		{
			perm_path = argv[i + 1];
			i++;
		}
		//else if (!strcmp(argv[i], "-p"))
		//{
		//	parallel_analysis = 1;
		//}
		else if (!strcmp(argv[i], "-candidates"))
		{
			candidates = true;
		}
		else if (!strcmp(argv[i], "-clk"))
		{
			clk_high = true;
		}
		else if (!strcmp(argv[i], "-HW"))
		{
			HW = true;
		}
		else if (!strcmp(argv[i], "-HD"))
		{
			HD = true;
		}
		else if (!strcmp(argv[i], "-SNR"))
		{
			SNR = true;
		}
		else if (!strcmp(argv[i], "-no_key_exp"))
		{
			key_expansion = false;
		}
		else if (!strcmp(argv[i], "-nv"))
		{
			verbose = -1;
		}
		else if (!strcmp(argv[i], "-v"))
		{
			verbose = 1;
		}
		else if (!strcmp(argv[i], "-perm"))
		{
			permutations = std::stoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-steps"))
		{
			steps = std::stoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-steps_start"))
		{
			steps_start = std::stoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-steps_stop"))
		{
			steps_stop = std::stoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-rate_stop"))
		{
			rate_stop = std::stof(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-threads"))
		{
			threads = std::stoi(argv[i + 1]);
			i++;
		}
		else 
		{
			std::cerr<<std::endl<<argv[i]<<wrong_input_msg;
			return 1;
		}
	}

	// Make sure the user provides a data file and ciphertext file
	if (!data_path_set || !ct_path_set)
	{
		std::cerr << input_msg;
		return 1;
	}

	// make sure the user provides the power model file and the cells type file together, if any
	if ((power_model_path_set && !cells_type_path_set) || (cells_type_path_set && !power_model_path_set))
	{
		std::cerr << input_msg;
		return 1;
	}

	// make sure the user provides steps to be considered in case a permutations file is provided
	if ((perm_path != "") && (steps == -1)) {

		std::cerr << "When providing a permutation file, make sure to provide the -steps parameter as well according to that permutation file!" << std::endl;
		std::cerr << " Note that you can also further specify -steps_start and/or -steps_stop to skip some steps." << std::endl;
		return 1;
	}
	// make sure the user provides permutations to be considered in case a permutations file is provided
	if ((perm_path != "") && (permutations == -1)) {

		std::cerr << "When providing a permutation file, make sure to provide the -perm parameter as well, to specify the number of permutations to be considered per step!" << std::endl;
		std::cerr << " Note that setting -perm to N with N being less than M permutations available in the permutation file means that only the first N permutations are considered for each step." << std::endl;
		return 1;
	}

	// Set parameters to default values if not provided by user
	if (permutations == -1) {
		permutations = 1;
	}
	if (steps == -1) {
		steps = 1;
	}
	if (steps_stop == -1) {
		steps_stop = steps;
	}
	if (steps_start == -1) {
		steps_start = 1;
	}
	if (!power_model_path_set && !cells_type_path_set && !HW && !HD) {
		HD = true;
	}
	if (threads == -1) {
		threads = 16;
	}
	omp_set_num_threads(threads);
	// rate_stop not be initialized if not provided; -1 here means that it should not be used

	// track start time
	auto start_time = std::chrono::system_clock::now();

	//// Decide whether to use the CPU or GPU algorithm
	//if (!parallel_analysis)
	//{
		cpa::cpa(data_path, ct_path, power_model_path, cells_type_path, clk_high, key_path, perm_path, HW, HD, SNR, candidates, permutations, steps, steps_start, steps_stop, rate_stop, verbose, key_expansion);
	//}
	//else
	//{
	//	#if defined(OPENCL) || defined(CUDA)
	//	cpa::pcpa(data_path, ct_path);
	//	#else
	//	std::cerr<<no_gpu_found;
	//	return 1;
	//	#endif
	//}	

	std::chrono::duration<double> runtime = std::chrono::system_clock::now() - start_time;
	std::cout << "Overall runtime for sequential: " << runtime.count() << " seconds" << std::endl;
	std::cout << std::endl;


	start_time = std::chrono::system_clock::now();
	cpaP::cpaP(data_path, ct_path, power_model_path, cells_type_path, clk_high, key_path, perm_path, HW, HD, SNR, candidates, permutations, steps, steps_start, steps_stop, rate_stop, verbose, key_expansion);
	runtime = std::chrono::system_clock::now() - start_time;
	std::cout << "Overall runtime for parallel: " << runtime.count() << " seconds" << std::endl;
	std::cout << std::endl;

	
	return 0;
}
