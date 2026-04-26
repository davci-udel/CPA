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

#include <ctime>
#include <iostream>
#include <bitset>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <omp.h>
#include <chrono>
#include <random>
#include <algorithm>
#include <iomanip>
#include <fstream>

#include "../common/aes-op.hpp"
#include "../common/csv_read.hpp"
#include "cpaP.hpp"
#include "cpa.hpp"
#include "power-models.hpp"
#include "stats.hpp"

// (TODO) refactor parameters into struct
void cpaP::cpaP(std::string data_path, std::string ct_path, std::string power_model_path, std::string cells_type_path, bool clk_high, std::string key_path, std::string perm_path, bool HW, bool HD, bool SNR_flag, bool candidates, int permutations, int steps, int steps_start, int steps_stop, float rate_stop, int verbose, bool key_expansion)
{
	#pragma omp parallel 
	{
		#pragma omp single
		std::cout << "Thread count: " << omp_get_num_threads() << std::endl;
	}
	const int num_bytes = 16;
	const int num_keys = 256;	

	size_t num_traces;

	int candidate;

	bool perm_file_write = false;
	bool perm_file_read = false;
	std::fstream perm_file;

	unsigned round_key_index = 10;

	// Obtain a time-based seed
	unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();

	// Prepare vectors
	std::vector< std::vector<float> > data; // power traces
	std::vector< std::vector<unsigned char> > ciphertext;
	std::vector< std::vector<unsigned char> > correct_key;
	std::vector< std::vector<unsigned char> > correct_round_key (11, std::vector<unsigned char> (num_bytes));
	std::vector< std::vector<unsigned char> > cipher (4, std::vector<unsigned char> (4));
	std::vector< std::vector<unsigned int> > perm_from_file;

	// prepare power model
	std::unordered_multimap< unsigned int, cpa::power_table_FF > power_model; // key is state bit index [0..127], value_s_ (multimap) are all power values of related cell

	// Print information to terminal
	//std::cout<<"\n\nMethod of Analysis: CPA";
	//std::cout<<"\nUsing GPU: NO";
	std::cout<<"\n";
	std::cout<<"Reading power traces from: "<<data_path;
	std::cout<<"\n";
	std::cout<<"Reading ciphertexts from: "<<ct_path;
	std::cout<<std::endl;

	// Read in ciphertext and power data
	auto start_time = std::chrono::system_clock::now();
	csv::read_data(data_path, data);
	csv::read_hex(ct_path, ciphertext);
	std::cout << "Overall runtime for read_data and hex: " << (std::chrono::system_clock::now() - start_time).count() << " ns" << std::endl;
	start_time = std::chrono::system_clock::now();
	// Record the number of traces and the
	// number of points per trace
	num_traces = data.size();

	if (power_model_path != "") {
		std::cout<<"Reading power model from: " << power_model_path << " and " << cells_type_path;
		std::cout<<"\n";

		// Read in the power model
		csv::read_power_model(power_model_path, cells_type_path, clk_high, power_model);
		std::cout << "Overall runtime for read_power: " << (std::chrono::system_clock::now() - start_time).count() << " ns" << std::endl;
		start_time = std::chrono::system_clock::now();
	}

//	// dbg
//	for (unsigned int i = 0; i < 128; i++) {
//
//		std::cout << "State Bit: " << i;
//		std::cout << std::endl;
//
//
//		std::cout << " Related power table:\n";
//
//		auto key_range = power_model.equal_range(i);
//		for (auto iter = key_range.first; iter != key_range.second; ++iter) {
//
//			std::cout << "  Cell: " << iter->second.cell << "\n";
//			std::cout << "   CDN: " << iter->second.CDN << "\n";
//			std::cout << "   CP: " << iter->second.CP << "\n";
//			std::cout << "   D: " << iter->second.D << "\n";
//			std::cout << "   Q: " << iter->second.Q << "\n";
//			std::cout << "   Value: " << iter->second.value << "\n";
//		}
//
//		std::cout << std::endl;
//	}

	// Read in the correct key, if provided 
	if (key_path != "") {

		std::cout<<"Reading correct key from: " << key_path << ": ";

		csv::read_hex(key_path, correct_key);

		for (unsigned int i = 0; i < num_bytes; i++)
			std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(correct_key[0][i]) << " ";
		std::cout<<std::endl;

		if (key_expansion) {
			std::cout<<" That key is considered to be a full key; key expansion is applied" << std::endl;
		}
		else {
			std::cout<<" That key is considered to be a round-10 key; key expansion is NOT applied" << std::endl;
		}

		// also derive round keys
		//
		// note that correct_round_key[0] contains the initial key
		aes::key_expand(correct_key[0], correct_round_key);

		// now, in case key expansion shall not be considered, the relevant key to look at is correct_round_key[0], the initial key
		if (!key_expansion) {
			round_key_index = 0;
		}

		// log round keys only for key expansion
		if (key_expansion) {
			for (unsigned round = 1; round < 11; round++) {
				std::cout << "  Related round-" << std::dec << round << " key: ";
				for (unsigned int i = 0; i < num_bytes; i++)
					std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(correct_round_key[round][i]) << " ";
				std::cout<<std::endl;
			}
		}
	}
	std::cout << "Overall runtime for key prep: " << (std::chrono::system_clock::now() - start_time).count() << " ns" << std::endl;
	start_time = std::chrono::system_clock::now();
	// Handle the permutations file, if provided
	if (perm_path != "") {

		std::cout<<"Handle permutations file: " << perm_path << std::endl;

		// try to read in permutations from file
		perm_file.open(perm_path.c_str(), std::ifstream::in);

		if (perm_file.is_open()) {
			perm_file_read = true;

			// dummy reading of "step 0", to initialize file parsing
			csv::read_perm_file(perm_file, 0, 0, 0, 0, perm_from_file);

			// dummy reading/dropping of all earlier steps which are not required for current call
			for (int drop = 1; drop < steps_start; drop++) {
				csv::read_perm_file(perm_file, drop, 0, 0, 0, perm_from_file);
			}
		}
		// if reading failed, consider to write out to the perm_file
		else {
			perm_file_write = true;

			perm_file.open(perm_path.c_str(), std::fstream::out);

			std::cout << "Reading of permutations file failed; permutations are generated randomly and written out to this file" << std::endl;
		}
	}
	std::cout << "Overall runtime for permutation prep: " << (std::chrono::system_clock::now() - start_time).count() << " ns" << std::endl;
	start_time = std::chrono::system_clock::now();
	// Prepare main vectors
	std::cout<<"\n";
	std::cout<<"Allocating memory...\n";
	std::cout<<std::endl;
	//std::vector< std::multimap<float, unsigned char, std::greater<float>> > r_pts
	//	( num_bytes, std::multimap<float, unsigned char, std::greater<float>> () );
	std::vector< std::multimap<float, unsigned char> > r_pts
		( num_bytes, std::multimap<float, unsigned char> () );
	std::vector<float> power_pts (num_traces, 0.0f);
	std::vector< std::vector< std::vector<float> > > Hamming_pts 
		( num_traces, std::vector< std::vector<float> > 
		(num_bytes, std::vector<float> (256, 0.0f) ) );
	std::vector< std::vector< std::vector<unsigned int> > > Hamming_pts__0_1_flips
		( num_traces, std::vector< std::vector<unsigned int> > 
		(num_bytes, std::vector<unsigned int> (256, 0) ) );
	std::vector< std::vector< std::vector<unsigned int> > > Hamming_pts__1_0_flips
		( num_traces, std::vector< std::vector<unsigned int> > 
		(num_bytes, std::vector<unsigned int> (256, 0) ) );

	std::vector<unsigned int> trace_indices (num_traces);
	std::cout << "Overall runtime for initial setup: " << (std::chrono::system_clock::now() - start_time).count() << " ns" << std::endl;
	start_time = std::chrono::system_clock::now();
	// Prepare with all trace indices; required for shuffling/generating permutations in case they are not read in
	#pragma omp parallel for
	for (unsigned int i = 0; i < num_traces; i++) {
		trace_indices[i] = i;
	}
	std::cout << "Overall runtime for trace_indice: " << (std::chrono::system_clock::now() - start_time).count() << " ns" << std::endl;
	start_time = std::chrono::system_clock::now();

	std::cout<<"Determine peak power values...\n";
	std::cout<<std::endl;

	// Consider the maximum power point as the leakage point
	//
	// search the whole trace
	float avg_max_pt = 0.0f;
	#pragma omp parallel for reduction(+:avg_max_pt)
	for (unsigned int i = 0; i < num_traces; i++)
	{
		float local_max_pt = 0.0f;
		for (unsigned int j = 0; j < data.at(i).size(); j++)
		{
			float local_data_pt = data.at(i).at(j);
			if (local_max_pt < local_data_pt)
				local_max_pt = local_data_pt;
		}
	
		power_pts[i] = local_max_pt;
		avg_max_pt += local_max_pt;
	}
	std::cout << "Overall runtime for power loop: " << (std::chrono::system_clock::now() - start_time).count() << " ns" << std::endl;
	start_time = std::chrono::system_clock::now();
	avg_max_pt /= num_traces;
	std::cout << std::dec << "Traces = " << num_traces << std::endl;
	std::cout << std::dec << "Avg peak power = " << avg_max_pt << std::endl;
	std::cout << std::endl;
			
	//// NICV, SNR computation
	if (SNR_flag) {

		std::cout<<"Calculate SNR ...\n";

		// metrics to be computed
		std::vector<float> SNR = std::vector<float>(16, 0.0f);
		std::vector<float> NICV = std::vector<float>(16, 0.0f);
		float NICV_; // intermediate variable, not actual NICV

		// groups for traces; related to partitioning for specific TVLA, NICV
		std::vector<float> G1 (num_traces, 0.0f);
		std::vector<float> G2 (num_traces, 0.0f);

		// stats on whole set of traces
		float mean, std_dev, var;
		stats::stats(power_pts, mean, std_dev, var);

		// compute NICV, SNR separately for each sub-key, key byte; follows principle of actual attacks
		#pragma omp parallel for
		for (int j = 0; j < 16; j++) {

			NICV_ = 0;

			// for each possible value in a byte
			for (int k = 0; k < 256; k++) {

				G1.clear();
				G2.clear();

				// 1) partition traces; for specific TVLA, NICV
				//
				for (unsigned int i = 0; i < num_traces; i++) {

					// extract jth byte of relevant intermediate/round

					// TODO extract (bytes of) relevant intermediate/round data from file, to be generated
					// from power sim testbench

					// Get cipher for this particular trace
					for (int l = 0; l < 4; l++)
						for (int m = 0; m < 4; m++)
							cipher[m][l] = ciphertext.at(i).at(l * 4 + m);

					int post_row;
					int post_col;
					//unsigned char post_byte;

					int pre_row;
					int pre_col;
					unsigned char pre_byte;

					unsigned char key_byte;

					// Select byte
					post_row = j / 4;
					post_col = j % 4;
					//post_byte = cipher[post_row][post_col];

					// Undo AES-128 operations, using the correct key byte
					key_byte = correct_round_key[round_key_index][j];
					aes::shift_rows(post_row, post_col, pre_row, pre_col);
					pre_byte = cipher[pre_row][pre_col];
					pre_byte = aes::add_round_key(key_byte, pre_byte);
					pre_byte = aes::inv_sub_bytes(pre_byte);

					//// dbg
					//std::cout << "j = " << j;
					//std::cout << "; ";
					//std::cout << "k = " << k;
					//std::cout << "; ";
					//std::cout << "i = " << i;
					//std::cout << "; ";
					//std::cout << "pre_byte = " << std::bitset<8>(pre_byte);
					//std::cout << std::endl;

					// actual partitioning
					if (pre_byte == k) {
						G1.push_back(power_pts[i]);
					}
					else {
						G2.push_back(power_pts[i]);
					}
				}

				// sanity check: calculations are only meaningful if also the smaller G1 holds some elements
				if (G1.empty()) {

					std::cout << "Warning: G1 is empty for byte j = " << j;
					std::cout << ", ";
					std::cout << " byte value k = " << k;
					std::cout << "; ";
					std::cout << " this indicates that the total number of traces is too small. Skipping this case for NICV, SNR calculations.";
					std::cout << std::endl;

					continue;
				}

				// 2) calculate partition statistics
				//
				float G1_mean, G1_std_dev, G1_var;
				float G2_mean, G2_std_dev, G2_var;

				stats::stats(G1, G1_mean, G1_std_dev, G1_var);
				stats::stats(G2, G2_mean, G2_std_dev, G2_var);

				//// dbg
				//std::cout << std::dec << "mean(G1) = " << G1_mean;
				//std::cout << "; ";
				//std::cout << std::dec << "mean(G2) = " << G2_mean;
				//std::cout << std::endl;
				//std::cout << std::dec << "std_dev(G1) = " << G1_std_dev;
				//std::cout << "; ";
				//std::cout << std::dec << "std_dev(G2) = " << G2_std_dev;
				//std::cout << std::endl;
				//std::cout << std::dec << "|G1| = " << G1.size();
				//std::cout << "; ";
				//std::cout << std::dec << "|G2| = " << G2.size();
				//std::cout << "; ";
				//std::cout << std::dec << "|G1| + |G2| = " << G1.size() + G2.size();
				//std::cout << std::endl;

				// 3) compute NICV_k; can be computed from TVLA or directly -- here we compute it directly
				//
				float NICV_k;
	//			float TVLA;
	//
	//			TVLA = (G1_mean - G2_mean) /
	//				std::sqrt( ( G1_var / G1.size() ) + ( G2_var / G2.size() ) );
	//
	//			// dbg
	//			std::cout << std::dec << "TVLA = " << TVLA;
	//			std::cout << std::endl;

				NICV_k =
					( G1.size() - ( std::pow(G1.size(), 2.0f) / G2.size() ) ) * std::pow(G1_mean - mean, 2.0f)
					+
					G2.size() * std::pow(G2_mean - mean, 2.0f)
				;

				//// dbg
				//std::cout << std::dec << "NICV_k = " << NICV_k;
				//std::cout << std::endl;

				NICV_ += NICV_k;

			} // for each possible value in a byte

			NICV[j] = NICV_ / (num_traces * var);
			SNR[j] = 1.0f / ( (1.0f/NICV[j]) - 1.0f );

			std::cout << std::dec << "Byte j : " << j;
			std::cout << std::endl;
			std::cout << " NICV = " << NICV[j];
			std::cout << " ; SNR = " << SNR[j];
			std::cout << " ; (1 / SNR) = " << (1.0f/SNR[j]);
			std::cout << std::endl;

		} // compute SNR separately for each sub-key, key byte; follows principle of actual attacks

		// compute/derive SNR stats from sorted SNR results
		std::sort(SNR.begin(), SNR.end());

		std::cout << "Overall:";
		std::cout << std::endl;

		std::cout << " ";
		std::cout << std::dec << "SNR_min = " << SNR[0];
		std::cout << std::endl;

		std::cout << " ";
		std::cout << std::dec << "SNR_max = " << SNR[15];
		std::cout << std::endl;

		std::cout << " ";
		std::cout << std::dec << "SNR_med (avg of sorted(SNR[7]), sorted(SNR[8])) = " << (SNR[7] + SNR[8]) / 2.0f;
		std::cout << std::endl;

		std::cout << " ";
		std::cout << std::dec << "(1 / SNR_min) = " << (1.0f/SNR[0]);
		std::cout << std::endl;

		std::cout << " ";
		std::cout << std::dec << "(1 / SNR_max) = " << (1.0f/SNR[15]);
		std::cout << std::endl;

		std::cout << " ";
		std::cout << std::dec << "(1 / SNR_med) = " << (2.0f/(SNR[7] + SNR[8]));
		std::cout << std::endl;
		std::cout << std::endl;

	//	std::cout << "Exiting...";
	//	std::cout << std::endl;
	//	exit(0);
	}
	std::cout << "Overall runtime for NICV: " << (std::chrono::system_clock::now() - start_time).count() << " ns" << std::endl;
	start_time = std::chrono::system_clock::now();

	if (HW) {
		std::cout<<"Calculate Hamming weights...\n";
	}
	else if (HD) {
		std::cout<<"Calculate Hamming distances...\n";
	}
	else {
		std::cout<<"Calculate power-model distances...\n";
	}
	std::cout<<std::endl;
			
	// Calculate Hamming points
	#pragma omp parallel for schedule(dynamic)
	for (unsigned int i = 0; i < num_traces; i++)
	{
		// Get cipher for this particular trace
		std::vector< std::vector<unsigned char> > local_cipher (4, std::vector<unsigned char> (4));
		for (int j = 0; j < 4; j++)
			for (int k = 0; k < 4; k++)
				local_cipher[k][j] = ciphertext.at(i).at(j * 4 + k);

		// Find ciphertext bytes at different stages for the Hamming point calculation
		for (int j = 0; j < num_bytes; j++)
		{
			int post_row;
			int post_col;
			unsigned char post_byte;

			// Select byte
			post_row = j / 4;
			post_col = j % 4;
			post_byte = local_cipher[post_row][post_col];

			// Create all possible bytes that could have resulted selected byte
			for (int k = 0; k < num_keys; k++)
			{
				int pre_row;
				int pre_col;
				unsigned int byte_id;
				unsigned char pre_byte;
				unsigned char key_byte;

				// Undo AES-128 operations
				key_byte = static_cast<unsigned char> (k);
				aes::shift_rows(post_row, post_col, pre_row, pre_col);
				pre_byte = local_cipher[pre_row][pre_col];
				pre_byte = aes::add_round_key(key_byte, pre_byte);
				pre_byte = aes::inv_sub_bytes(pre_byte);
				byte_id = pre_col * 4 + pre_row;
	
				// apply the power model
				if (HW) {
					Hamming_pts[i][byte_id][k] = pm::Hamming_weight(pre_byte);
				}
				else if (HD) {
					Hamming_pts[i][byte_id][k] = pm::Hamming_dist(pre_byte, post_byte);
				}
				else {
					Hamming_pts[i][byte_id][k] = pm::power(pre_byte, post_byte, byte_id, power_model, clk_high);
				}

				// also track the 0->1 and 1->0 flips
				Hamming_pts__0_1_flips[i][byte_id][k] = 0;
				Hamming_pts__1_0_flips[i][byte_id][k] = 0;
				for (unsigned char b = 1 << 7; b > 0; b = b / 2)  {
					// 0->1
					if (!(pre_byte & b) && (post_byte & b))
						Hamming_pts__0_1_flips[i][byte_id][k]++;
					// 1->0
					if ((pre_byte & b) && !(post_byte & b))
						Hamming_pts__1_0_flips[i][byte_id][k]++;
				}
			}
		}
	}
	std::cout << "Overall runtime for hamming points: " << (std::chrono::system_clock::now() - start_time).count() << " ns" << std::endl;
	start_time = std::chrono::system_clock::now();

	// Consider multiple runs, as requested by step_size parameter
	//
	// s has to be float, to properly calculate data_pts
	
	for (float s = steps_start; s <= steps_stop; s++) {

		int data_pts = num_traces * (s / steps);

		std::cout << "Working on step " << std::dec << s << "...\n";

		float data_pts_perc = data_pts;
		data_pts_perc /= num_traces;
		data_pts_perc *= 100;
		std::cout << "(" << data_pts << " / " << num_traces << ") traces = " << data_pts_perc << " % of all traces\n";
		std::cout<<std::endl;

		float success_rate = 0.0f;
		float key_HD = 0.0f;
		std::vector<float> key_byte_correlation_HD (num_bytes, 0.0f);
		std::vector<unsigned char> round_key (num_bytes);
		std::vector<float> max_correlation (num_bytes, 0.0f);

		float avg_HD = 0.0f;
		float avg_0_1_flips = 0.0f;
		float avg_1_0_flips = 0.0f;
		float avg_flips_HD = 0.0f;
		float avg_flips_HD_bytes = 0.0f;
		float std_dev_HD = 0.0f;
		float std_dev_0_1_flips = 0.0f;
		float std_dev_1_0_flips = 0.0f;
		float std_dev_flips_HD = 0.0f;
		float std_dev_flips_HD_bytes = 0.0f;

		// track correlation for all 256 key candidates and the correct key separately; correlation for correct key is in
		// avg_correlation[256]
		std::vector<float> avg_correlation (num_keys + 1, 0.0f);

		// init permutations vector from file
		if (perm_file_read) {

			csv::read_perm_file(perm_file, s, data_pts, permutations, num_traces, perm_from_file);
		}

		// Consider multiple runs, as requested by permutations parameter
		for (int perm = 1; perm <= permutations; perm++) {

			// reset Pearson correlation multimaps
			for (int i = 0; i < num_bytes; i++) {
				r_pts[i].clear();
			}

			if (verbose == 1) {
				std::cout<<" Start permutation #" << std::dec << perm << "...\n";
				std::cout<<std::endl;
			}

			// in case pre-defined permutations have been read in, use those
			if (perm_file_read) {

				trace_indices = perm_from_file[perm - 1];
			}
			// otherwise, consider a random one
			else {
				shuffle(trace_indices.begin(), trace_indices.end(), std::default_random_engine(seed));

				// also write out the newly generated, random permutation
				if (perm_file_write) {

					// write out markers for simpler parsing
					if (perm == 1) {
						perm_file << "STEP_START\n";
					}
					perm_file << "PERM_START\n";

					// only write out the first #(data_pts) as needed
					for (int d = 0; d < data_pts; d++) {
						perm_file << trace_indices[d] << " ";
					}
					// one permutation per line (only for readability, not required for parsing)
					perm_file << "\n";
				}
			}

			// Perform Pearson r correlation for this permutation's Hamming points and power data

			if (verbose == 1) {
				std::cout<<"  Calculate Pearson correlation...\n";
				std::cout<<std::endl;
			}

			// Derive correlation per key byte, to handle complexity, by decomposing into 16 * 2^8 candidates; ideally, correlation
			// should be over all possible 2^128 key candidates but that's intractable
			//
			std::vector< std::vector<float>> temp_r_pts ( num_bytes, std::vector<float> (num_keys) );
			#pragma omp parallel for collapse(2) schedule(dynamic, 1)
			for (int i = 0; i < num_bytes; i++)
			{	
				for (int j = 0; j < num_keys; j++)
				{
					std::vector<float> column(num_traces);
					for (size_t t = 0; t < num_traces; t++)
						column[t] = Hamming_pts[t][i][j];

					temp_r_pts[i][j] = stats::pearsonr(power_pts, column, trace_indices, data_pts);
				}
			}

			for (int i = 0; i < num_bytes; i++)
			{	
				for (int j = 0; j < num_keys; j++)
				{
					// Pearson r correlation with power data
					//
					r_pts[i].emplace( std::make_pair(temp_r_pts[i][j],	j));
				}
			}

			if (verbose == 1) {
				std::cout<<"  Derive the key candidates...\n";
				std::cout<<std::endl;
			}

			// The keys will be derived by considering all 256 options for each key byte, whereas the key bytes are ordered by
			// the correlation values -- note that this is different from deriving all possible keys (there, one would combine
			// all 256 options for key byte 0, with all 256 options for key byte 1, etc).

			// depending on the runtime parameter, consider all keys by starting from 0, or provide only the two most probable ones,
			// which are the last
			if (candidates) {
				candidate = 0;
			}
			else {
				candidate = num_keys - 2;
			}

			for (; candidate < num_keys; candidate++) {

				// track max and average correlation
				float avg_cor = 0.0;
				for (int i = 0; i < num_bytes; i++) {

					auto iter = r_pts[i].begin();
					std::advance(iter, candidate);

					round_key[i] = iter->second;
					max_correlation[i] = iter->first;

					avg_cor += max_correlation[i];
				}
				avg_cor /= num_bytes;

				// track avg correlation for all candidates across all permutations
				avg_correlation[candidate] += avg_cor;

				if (verbose == 1) {

					// Report the key
					if (candidates) {
						std::cout<<"  Round-10 key candidate " << std::dec << candidate - num_keys + 1 << " (in hex): ";
					}
					else {
						std::cout<<"  Round-10 key prediction (in hex):  ";
					}
					for (unsigned int i = 0; i < num_bytes; i++)
						std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(round_key[i]) << " ";
					std::cout<<"\n";

					// Report the related correlation values
					std::cout<<"   Related Pearson correlation values are: ";

					for (unsigned int i = 0; i < num_bytes; i++) {
						std::cout << std::dec << max_correlation[i] << " ";
					}
					std::cout<<"\n";

					std::cout<<"    Avg Pearson correlation across all round-10 key bytes: " << avg_cor;
					std::cout<<"\n";

					if (key_expansion) {

						// Reverse the AES key scheduling to retrieve the original key
						std::vector<unsigned char> full_key (num_bytes);
						aes::inv_key_expand(round_key, full_key);

						std::cout<<"   Full key (in hex): ";
						for (unsigned int i = 0; i < num_bytes; i++)
							std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(full_key[i]) << " ";
						std::cout<<"\n";
					}
					std::cout<<std::endl;

				} // verbose
			} // candidate

			// if correct key was provided, evaluate success rate, HD values, and avg correlation for correct-key bytes; also track
			// flips
			//
			if (!correct_key.empty()) {
				bool success = true;

				// track success rate and actual HD for that last candidate; always concerning round-10 key (round_key)
				for (unsigned int i = 0; i < num_bytes; i++) {

					// check whether any byte is off
					if  (round_key[i] != correct_round_key[round_key_index][i]) {
						success = false;
					}

					// calculate the actual HD for the key candidate and the correct key
					if (verbose != -1) {
						key_HD += pm::Hamming_dist(round_key[i], correct_round_key[round_key_index][i]);
					}
				}

				if (success) {
					success_rate += 1;
				}

				// now, also track the correlation-related HD, i.e., for each round-key byte, track how far away was the
				// correct byte from the picked, most probable one
				//
				float avg_cor = 0.0;
				for (unsigned int i = 0; i < num_bytes; i++) {

					// check for the correct byte among all the candidates, not only the last one
					//
					// reverse order as correct byte is probably more toward the end
					for (int candidate = num_keys - 1; candidate >= 0; candidate--) {

						auto iter = r_pts[i].begin();
						std::advance(iter, candidate);
						round_key[i] = iter->second;

						if  (round_key[i] == correct_round_key[round_key_index][i]) {

							if (verbose != -1) {
								key_byte_correlation_HD[i] += ((num_keys - 1) - candidate);
							}

							//// dbg logging
							//std::cout << "byte: " << i;
							//std::cout << "; ";
							//std::cout << "candidate: " << candidate;
							//std::cout << "; ";
							//std::cout << "HD_candidate: " << ((num_keys - 1) - candidate);
							//std::cout << std::endl;
							
							// also track avg correlation for the correct key, by summing up the byte-level
							// correlation here
							max_correlation[i] = iter->first;
							avg_cor += max_correlation[i];

							// no need to check other candidates once the correct byte is found
							break;
						}
					}
				}

				// derive avg correlation for correct key bytes
				avg_correlation[num_keys] += (avg_cor / num_bytes);

				if (verbose != -1) {
					// track average HD and flips
					//
					float avg_HD_ = 0.0f;
					float avg_0_1_flips_ = 0.0f;
					float avg_1_0_flips_ = 0.0f;
					float avg_flips_HD_ = 0.0f;
					float avg_flips_HD_bytes_ = 0.0f;
					float std_dev_HD_ = 0.0f;
					float std_dev_0_1_flips_ = 0.0f;
					float std_dev_1_0_flips_ = 0.0f;
					float std_dev_flips_HD_ = 0.0f;
					float std_dev_flips_HD_bytes_ = 0.0f;

					for (int trace = 0; trace < data_pts; trace++) {

						float avg_HD__ = 0.0f;
						float avg_0_1_flips__ = 0.0f;
						float avg_1_0_flips__ = 0.0f;
						float avg_flips_HD_bytes__ = 0.0f;

						for (unsigned int i = 0; i < num_bytes; i++) {

							avg_HD__ += Hamming_pts
								[ trace_indices[trace] ]
								[i]
								[ correct_round_key[round_key_index][i] ]
									;

							avg_0_1_flips__ += Hamming_pts__0_1_flips
								[ trace_indices[trace] ]
								[i]
								[ correct_round_key[round_key_index][i] ]
									;

							avg_1_0_flips__ += Hamming_pts__1_0_flips
								[ trace_indices[trace] ]
								[i]
								[ correct_round_key[round_key_index][i] ]
									;

							avg_flips_HD_bytes__ += std::abs(static_cast<int>(
									Hamming_pts__0_1_flips
										[ trace_indices[trace] ]
										[i]
										[ correct_round_key[round_key_index][i] ]
									- 
									Hamming_pts__1_0_flips
										[ trace_indices[trace] ]
										[i]
										[ correct_round_key[round_key_index][i] ]
									));
						}

						// flips, normalize already here (should help to limit rounding errors for large number of
						// data_pts/traces)
						avg_0_1_flips_ += (avg_0_1_flips__ / data_pts);
						avg_1_0_flips_ += (avg_1_0_flips__ / data_pts);

						// HD between all flips for this text
						avg_flips_HD_ += (std::abs(avg_0_1_flips__ - avg_1_0_flips__) / data_pts);

						// HD between all flips for this text, but evaluated at byte level; normalized over bytes and also over
						// data_pts already here
						avg_flips_HD_bytes_ += (avg_flips_HD_bytes__ / num_bytes / data_pts);

						// regular HD, normalized over 128 bits and also over data_pts already here
						avg_HD_ += (avg_HD__ / 128 / data_pts);

						//// dbg logging
						////
						//std::cout << "DBG> Trace " << std::dec << (trace + 1) << "/" << data_pts << std::endl;
						//std::cout << "DBG>  0->1 flips: " << avg_0_1_flips__ << std::endl;
						//std::cout << "DBG>  1->0 flips: " << avg_1_0_flips__ << std::endl;
						//std::cout << "DBG>  HD between flips; text level: " << std::abs(avg_0_1_flips__ - avg_1_0_flips__) << std::endl;
						//std::cout << "DBG>  HD between flips; byte level: " << avg_flips_HD_bytes__ / num_bytes << std::endl;
						//// recover normalization to display actual values; normalize still over current trace
						//std::cout << "DBG> Avg 0->1 flips: " << (data_pts * avg_0_1_flips_ / (trace + 1)) << std::endl;
						//std::cout << "DBG> Avg 1->0 flips: " << (data_pts * avg_1_0_flips_ / (trace + 1)) << std::endl;
						//std::cout << "DBG> Avg HD between flips; text level: " << (data_pts * avg_flips_HD_ / (trace + 1)) << std::endl;
						//std::cout << "DBG> Avg HD between flips; byte level: " << (data_pts * avg_flips_HD_bytes_ / (trace + 1)) << std::endl;
						//std::cout << "DBG> " << std::endl;
					}

					// derive std dev values
					//
					for (int trace = 0; trace < data_pts; trace++) {

						float _HD = 0.0f;
						float _0_1_flips = 0.0f;
						float _1_0_flips = 0.0f;
						float _flips_HD_bytes = 0.0f;

						for (unsigned int i = 0; i < num_bytes; i++) {

							_HD += Hamming_pts
								[ trace_indices[trace] ]
								[i]
								[ correct_round_key[round_key_index][i] ]
									;

							_0_1_flips += Hamming_pts__0_1_flips
								[ trace_indices[trace] ]
								[i]
								[ correct_round_key[round_key_index][i] ]
									;

							_1_0_flips += Hamming_pts__1_0_flips
								[ trace_indices[trace] ]
								[i]
								[ correct_round_key[round_key_index][i] ]
									;

							_flips_HD_bytes += std::abs(static_cast<int>(
									Hamming_pts__0_1_flips
										[ trace_indices[trace] ]
										[i]
										[ correct_round_key[round_key_index][i] ]
									- 
									Hamming_pts__1_0_flips
										[ trace_indices[trace] ]
										[i]
										[ correct_round_key[round_key_index][i] ]
									));
						}

						std_dev_HD_ += std::pow((_HD / 128) - avg_HD_, 2.0f);
						std_dev_0_1_flips_ += std::pow(_0_1_flips - avg_0_1_flips_, 2.0f);
						std_dev_1_0_flips_ += std::pow(_1_0_flips - avg_1_0_flips_, 2.0f);
						std_dev_flips_HD_ += std::pow(std::abs(_0_1_flips - _1_0_flips) - avg_flips_HD_, 2.0f);
						std_dev_flips_HD_bytes_ += std::pow((_flips_HD_bytes / num_bytes) - avg_flips_HD_bytes_, 2.0f);
					}
					std_dev_HD_ = std::sqrt(std_dev_HD_ / data_pts);
					std_dev_0_1_flips_ = std::sqrt(std_dev_0_1_flips_ / data_pts);
					std_dev_1_0_flips_ = std::sqrt(std_dev_1_0_flips_ / data_pts);
					std_dev_flips_HD_ = std::sqrt(std_dev_flips_HD_ / data_pts);
					std_dev_flips_HD_bytes_ = std::sqrt(std_dev_flips_HD_bytes_ / data_pts);

					// sum up averages
					// normalized already above over data_pts/traces; will be normalized over permutations later on
					avg_HD += avg_HD_;
					avg_flips_HD += avg_flips_HD_;
					avg_flips_HD_bytes += avg_flips_HD_bytes_;
					avg_0_1_flips += avg_0_1_flips_;
					avg_1_0_flips += avg_1_0_flips_;
					// also sum up std dev; also to be normalized over permutations later on
					std_dev_HD += std_dev_HD_;
					std_dev_0_1_flips += std_dev_0_1_flips_;
					std_dev_1_0_flips += std_dev_1_0_flips_;
					std_dev_flips_HD += std_dev_flips_HD_;
					std_dev_flips_HD_bytes += std_dev_flips_HD_bytes_;

					if (verbose == 1) {
						std::cout << "  Statistics for text before last round and after last round (the latter is the ciphertext), considering the correct round-key, for this permutation" << std::endl;
						std::cout << "   Avg Hamming distance for texts: " << avg_HD_ * 100 << " %" << std::endl;
						std::cout << "    Std dev over all traces: " << std_dev_HD_ * 100 << " %" << std::endl;
						std::cout << "   Avg 0->1 flips, across whole text: " << avg_0_1_flips_ << std::endl;
						std::cout << "    Std dev over all traces: " << std_dev_0_1_flips_ << std::endl;
						std::cout << "   Avg 1->0 flips, across whole text: " << avg_1_0_flips_ << std::endl;
						std::cout << "    Std dev over all traces: " << std_dev_1_0_flips_ << std::endl;
						std::cout << "   Avg Hamming distance for flips, across whole text: " << avg_flips_HD_ << std::endl;
						std::cout << "    Std dev over all traces: " << std_dev_flips_HD_ << std::endl;
						std::cout << "   Avg Hamming distance for flips, across bytes of text: " << avg_flips_HD_bytes_ << std::endl;
						std::cout << "    Std dev over all traces: " << std_dev_flips_HD_bytes_ << std::endl;
						std::cout << "   Note that these two Hamming distances are computed for each trace and only then averaged, and note the following as well:" << std::endl;
						std::cout << "    a) that is essential as the imbalance of flips (expressed by Hamming distance) impacts the power estimation (Hamming distance model) directly" << std::endl;
						std::cout << "    b) therefore, the average Hamming distances _cannot_ be derived from the above reported averages for flips" << std::endl;
						std::cout << std::endl;
					} // extra verbose
				} // verbose

			} // correct key

			if (verbose == 1) {
				std::cout<<" Stop permutation #" << std::dec << perm << "...\n";
				std::cout<<std::endl;
			}
		} // perm

		// correlation stats for all candidates
		if (candidates) {
			candidate = 0;
		}
		else {
			candidate = num_keys - 2;
		}

		std::cout<<" Avg Pearson correlations, across all key bytes and permutations\n";

		for (; candidate < num_keys; candidate++) {

			//// with top two candidates logged (through num_keys - 2 above), this different notation is not useful
			//if (candidates) {
				std::cout<<"  For round-10 key candidate " << std::dec << candidate - num_keys + 1;
			//}
			//else {
			//	std::cout<<"  For round-10 key prediction";
			//}
			std::cout << ": " << avg_correlation[candidate] / permutations;
			std::cout<<"\n";
		}

		// more stats in case correct key was provided
		//
		if (!correct_key.empty()) {

			// correlation stats for correct key
			std::cout << "  For correct round-10 key: ";
			std::cout << avg_correlation[num_keys] / permutations;
			std::cout << "\n";
			std::cout << "\n";

			std::cout << " Success rate: ";
			std::cout << "(" << success_rate << " / " << permutations << ") = ";

			success_rate /= permutations;
			success_rate *= 100.0;

			std::cout << success_rate << " %";
			std::cout << "\n";

			if (verbose != -1) {

				std::cout << " Avg Hamming distance between correct round-key and round-key guess: ";
				std::cout << (key_HD / 128 / permutations) * 100.0 << " %";
				std::cout << "\n";

				std::cout << " Avg correlation-centric Hamming distances between correct round-key bytes and round-key byte guesses:\n";

				float key_byte_correlation_HD_overall = 0;
				for (unsigned int i = 0; i < num_bytes; i++) {

					std::cout << "  Byte " << std::dec << i << ": ";

					// each byte could be off by 255 at max, namely when the least probable candidate was the correct one
					std::cout << (key_byte_correlation_HD[i] / 255 / permutations) * 100.0 << " %";
					if (key_byte_correlation_HD[i] > 0) {
						std::cout << " (translates to being off by " << (key_byte_correlation_HD[i] / permutations) << " candidates)";
					}
					std::cout << "\n";

					// also track overall HD
					key_byte_correlation_HD_overall += key_byte_correlation_HD[i];
				}
				std::cout << "   Avg across all bytes: ";
				std::cout << (key_byte_correlation_HD_overall / (num_bytes * 255) / permutations) * 100.0 << " %";
				if (key_byte_correlation_HD_overall > 0) {
					std::cout << " (translates to being off by " << (key_byte_correlation_HD_overall / num_bytes / permutations) << " candidates)\n";
				}
				else {
					std::cout << "\n";
				}

				// normalize over permutations
				avg_HD /= permutations;
				avg_0_1_flips /= permutations;
				avg_1_0_flips /= permutations;
				avg_flips_HD /= permutations;
				avg_flips_HD_bytes /= permutations;
				std_dev_HD /= permutations;
				std_dev_0_1_flips /= permutations;
				std_dev_1_0_flips /= permutations;
				std_dev_flips_HD /= permutations;
				std_dev_flips_HD_bytes /= permutations;

				std::cout << "  Statistics for text before last round and after last round (the latter is the ciphertext), considering the correct round-key, over all permutations" << std::endl;
				std::cout << "   Avg Hamming distance for texts: " << avg_HD * 100 << " %" << std::endl;
				std::cout << "    Avg of std dev (over all traces per permutation): " << std_dev_HD * 100 << " %" << std::endl;
				std::cout << "   Avg 0->1 flips, across whole text: " << avg_0_1_flips << std::endl;
				std::cout << "    Avg of std dev (over all traces per permutation): " << std_dev_0_1_flips << std::endl;
				std::cout << "   Avg 1->0 flips, across whole text: " << avg_1_0_flips << std::endl;
				std::cout << "    Avg of std dev (over all traces per permutation): " << std_dev_1_0_flips << std::endl;
				std::cout << "   Avg Hamming distance for flips, across whole text: " << avg_flips_HD << std::endl;
				std::cout << "    Avg of std dev (over all traces per permutation): " << std_dev_flips_HD << std::endl;
				std::cout << "   Avg Hamming distance for flips, across bytes of text: " << avg_flips_HD_bytes << std::endl;
				std::cout << "    Avg of std dev (over all traces per permutation): " << std_dev_flips_HD_bytes << std::endl;
				std::cout << "   Note that these two Hamming distances are computed for each trace and only then averaged, and note the following as well:" << std::endl;
				std::cout << "    a) that is essential as the imbalance of flips (expressed by Hamming distance) impacts the power estimation (Hamming distance model) directly" << std::endl;
				std::cout << "    b) therefore, the average Hamming distances _cannot_ be derived from the above reported averages for flips" << std::endl;
			} // verbose
		}
		// correct key was not provided; report on key prediction
		else {
			std::cout<<" Round-10 key prediction (in hex); just the one from the last permutation considered for that step:  ";
			for (unsigned int i = 0; i < num_bytes; i++)
				std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(round_key[i]) << " ";
			std::cout<<"\n";

			if (key_expansion) {

				// Reverse the AES key scheduling to retrieve the original key
				std::vector<unsigned char> full_key (num_bytes);
				aes::inv_key_expand(round_key, full_key);

				std::cout<<"  Full key (in hex): ";
				for (unsigned int i = 0; i < num_bytes; i++)
					std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(full_key[i]) << " ";
				std::cout<<"\n";
			}
		}
		std::cout << std::endl;

		// in case the correct key and a stop rate is provided, abort steps once that success rate is reached
		//
		if (!correct_key.empty()) {

			if ((rate_stop != -1) && (success_rate >= rate_stop)) {
				break;
			}
		}
	}
	std::cout << "Overall runtime for correlation analysis: " << (std::chrono::system_clock::now() - start_time).count() << " ns" << std::endl;
	start_time = std::chrono::system_clock::now();

	if (perm_file_write) {
		perm_file.close();
	}
}
		
