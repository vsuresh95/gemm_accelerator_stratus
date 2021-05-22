// Copyright (c) 2011-2021 Columbia University, System Level Design Group
// SPDX-License-Identifier: Apache-2.0

#include "gemm_accelerator.hpp"
#include "gemm_accelerator_directives.hpp"

// Functions

#include "gemm_accelerator_functions.hpp"

// Processes

void gemm_accelerator::load_input()
{

    // Reset
    {
        HLS_PROTO("load-reset");

        this->reset_load_input();

        // explicit PLM ports reset if any

        // User-defined reset code

        wait();
    }

    // Config
    /* <<--params-->> */
    int32_t gemm_m;
    int32_t gemm_n;
    int32_t gemm_k;
    {
        HLS_PROTO("load-config");

        cfg.wait_for_config(); // config process
        conf_info_t config = this->conf_info.read();

        // User-defined config code
        /* <<--local-params-->> */
        gemm_m = config.gemm_m;
        gemm_n = config.gemm_n;
        gemm_k = config.gemm_k;
    }

    // Load
    {
        HLS_PROTO("load-dma");
        wait();

        bool ping = true;

        // Moving in M dimension for matrix 1, and moving to new row of output
        for (uint32_t num_m = 0; num_m < gemm_m/BLOCK_SIZE; num_m++)
        {
            wait();
            // Moving in N dimension for matrix 2, and moving to new column of output
            for (uint32_t num_n = 0; num_n < gemm_n/BLOCK_SIZE; num_n++)
            {
                wait();
                // Moving in K dimension for both matrices to fully compute output
                for (uint32_t num_k = 0; num_k < gemm_k/BLOCK_SIZE; num_k++)
                {
                    wait();
                    // read the two 64x64 blocks from matrix 1 & 2 - only offset's change depending on 
                    // position in outer loops
                    for (uint32_t mat_num = 0; mat_num < 2; mat_num++)
                    {
                        uint32_t offset;

                        wait();

                        // offset from start + vertical offset + horizontal offset
                        if (mat_num)
                            offset = (gemm_m * gemm_k) + (num_n * BLOCK_SIZE * gemm_k) + (num_k * BLOCK_SIZE);
                        else
                            offset = (num_m * BLOCK_SIZE * gemm_k) + (num_k * BLOCK_SIZE);
                    
                        // each new row of the block
                        for (uint32_t row_num = 0; row_num < BLOCK_SIZE; row_num++)
                        {
                            wait();

                            dma_info_t dma_info(offset / DMA_WORD_PER_BEAT, BLOCK_SIZE / DMA_WORD_PER_BEAT, DMA_SIZE);
                            this->dma_read_ctrl.put(dma_info);
                            offset += gemm_k;

                            for (uint32_t i = 0; i < BLOCK_SIZE; i += DMA_WORD_PER_BEAT)
                            {
                                HLS_BREAK_DEP(plm_in_ping);
                                HLS_BREAK_DEP(plm_in_pong);

                                sc_dt::sc_bv<DMA_WIDTH> dataBv;

                                dataBv = this->dma_read_chnl.get();
                                wait();

                                // Write to PLM (all DMA_WORD_PER_BEAT words in one cycle)
                                for (uint32_t k = 0; k < DMA_WORD_PER_BEAT; k++)
                                {
                                    uint32_t plm_index = (mat_num * (PLM_IN_WORD/2)) + (row_num * BLOCK_SIZE) + i + k;
                                    HLS_UNROLL_SIMPLE;
                                    if (ping)
                                        plm_in_ping[plm_index] = dataBv.range((k+1) * DATA_WIDTH - 1, k * DATA_WIDTH).to_int64();
                                    else
                                        plm_in_pong[plm_index] = dataBv.range((k+1) * DATA_WIDTH - 1, k * DATA_WIDTH).to_int64();
                                }
                            }
                        }
                    }
                    this->load_compute_handshake();
                    ping = !ping;
                }
            }
        }
    }

    // Conclude
    {
        this->process_done();
    }
}



void gemm_accelerator::store_output()
{
    // Reset
    {
        HLS_PROTO("store-reset");

        this->reset_store_output();

        // explicit PLM ports reset if any

        // User-defined reset code

        wait();
    }

    // Config
    /* <<--params-->> */
    int32_t gemm_m;
    int32_t gemm_n;
    int32_t gemm_k;
    {
        HLS_PROTO("store-config");

        cfg.wait_for_config(); // config process
        conf_info_t config = this->conf_info.read();

        // User-defined config code
        /* <<--local-params-->> */
        gemm_m = config.gemm_m;
        gemm_n = config.gemm_n;
        gemm_k = config.gemm_k;
    }

    // Store
    {
        HLS_PROTO("store-dma");
        wait();

        bool ping = true;

        // Moving in M dimension to new row of output
        for (uint32_t num_m = 0; num_m < gemm_m/BLOCK_SIZE; num_m++)
        {
            wait();
            // Moving in N dimension to new column of output
            for (uint32_t num_n = 0; num_n < gemm_n/BLOCK_SIZE; num_n++)
            {
                this->store_compute_handshake();

                uint32_t offset = (gemm_m * gemm_k) + (gemm_n * gemm_k) + (num_m * BLOCK_SIZE * gemm_n) + (num_n * BLOCK_SIZE);

                // each new row of the block
                for (uint32_t row_num = 0; row_num < BLOCK_SIZE; row_num++)
                {
                    wait();

                    dma_info_t dma_info(offset / DMA_WORD_PER_BEAT, BLOCK_SIZE / DMA_WORD_PER_BEAT, DMA_SIZE);
                    offset += gemm_n;

                    this->dma_write_ctrl.put(dma_info);

                    for (uint32_t i = 0; i < BLOCK_SIZE ; i += DMA_WORD_PER_BEAT)
                    {
                        sc_dt::sc_bv<DMA_WIDTH> dataBv;

                        // Read from PLM
                        wait();
                        for (uint32_t k = 0; k < DMA_WORD_PER_BEAT; k++)
                        {
                            HLS_UNROLL_SIMPLE;
                            if (ping)
                                dataBv.range((k+1) * DATA_WIDTH - 1, k * DATA_WIDTH) = plm_out_ping[(row_num * BLOCK_SIZE) + i + k];
                            else
                                dataBv.range((k+1) * DATA_WIDTH - 1, k * DATA_WIDTH) = plm_out_pong[(row_num * BLOCK_SIZE) + i + k];
                        }
                        this->dma_write_chnl.put(dataBv);
                    }
                }
                ping = !ping;
            }
        }
    }

    // Conclude
    {
        this->accelerator_done();
        this->process_done();
    }
}


void gemm_accelerator::compute_kernel()
{
    // Reset
    {
        HLS_PROTO("compute-reset");

        this->reset_compute_kernel();

        // explicit PLM ports reset if any

        // User-defined reset code

        wait();
    }

    // Config
    /* <<--params-->> */
    int32_t gemm_m;
    int32_t gemm_n;
    int32_t gemm_k;
    {
        HLS_PROTO("compute-config");

        cfg.wait_for_config(); // config process
        conf_info_t config = this->conf_info.read();

        // User-defined config code
        /* <<--local-params-->> */
        gemm_m = config.gemm_m;
        gemm_n = config.gemm_n;
        gemm_k = config.gemm_k;
    }

    // Compute
    bool ping = true;
    bool ping_out = true;
    uint32_t N_BLOCK_M = gemm_m/BLOCK_SIZE;
    uint32_t N_BLOCK_N = gemm_n/BLOCK_SIZE;
    uint32_t N_BLOCK_K = gemm_k/BLOCK_SIZE;
    {
        // Moving in M dimension for matrix 1, and moving to new row of output
        for (uint32_t num_m = 0; num_m < N_BLOCK_M; num_m++)
        {
            // Moving in N dimension for matrix 2, and moving to new column of output
            for (uint32_t num_n = 0; num_n < N_BLOCK_N; num_n++)
            {
                // Moving in K dimension for both matrices to fully compute output
                for (uint32_t num_k = 0; num_k < N_BLOCK_K; num_k++)
                {
                    this->compute_load_handshake();

                    uint32_t regs_m[PLM_PORTS];
                    uint32_t regs_n[PLM_PORTS];
                    uint32_t regs_mul[PLM_PORTS];
                    uint32_t regs_acc[PLM_PORTS];
                    uint32_t regs_acc_0[8];
                    uint32_t regs_acc_1[4];
                    uint32_t regs_acc_2[2];
                    HLS_FLATTEN_ARRAY(regs_m);
                    HLS_FLATTEN_ARRAY(regs_n);
                    HLS_FLATTEN_ARRAY(regs_mul);
                    HLS_FLATTEN_ARRAY(regs_acc);
                    HLS_FLATTEN_ARRAY(regs_acc_0);
                    HLS_FLATTEN_ARRAY(regs_acc_1);
                    HLS_FLATTEN_ARRAY(regs_acc_2);

                    // Computing phase implementation
                    for (uint32_t m_block = 0; m_block < N_SUB_BLOCK; m_block++)
                    {
                        for (uint32_t n_block = 0; n_block < N_SUB_BLOCK; n_block++)
                        {
                            for (uint32_t k_block = 0; k_block < N_SUB_BLOCK; k_block++)
                            {
                                for (uint32_t m = 0; m < PLM_PORTS; m++)
                                {
                                    uint32_t out_offset = m_block*BLOCK_SIZE*PLM_PORTS + n_block*PLM_PORTS + m*BLOCK_SIZE;

                                    // read the previous partial sum, or not
                                    if (num_k == 0 && k_block == 0)
                                    {
                                        for (int elem_n_0 = 0; elem_n_0 < PLM_PORTS; elem_n_0++)
                                        {
                                            HLS_UNROLL_LOOP(ON, "read_plm_out_0");

                                            regs_acc[elem_n_0] = 0;
                                        }
                                    }
                                    else
                                    {
                                        for (int elem_n_1 = 0; elem_n_1 < PLM_PORTS; elem_n_1++)
                                        {
                                            HLS_UNROLL_LOOP(ON, "read_plm_out_1");
                                            HLS_BREAK_ARRAY_DEPENDENCY(plm_out_ping);
                                            HLS_BREAK_ARRAY_DEPENDENCY(plm_out_pong);

                                            uint32_t out_index = out_offset + elem_n_1;

                                            if (ping_out)
                                                regs_acc[elem_n_1] = plm_out_ping[out_index];
                                            else
                                                regs_acc[elem_n_1] = plm_out_pong[out_index];
                                        }
                                    }

                                    uint32_t m_offset = m_block*BLOCK_SIZE*PLM_PORTS + k_block*PLM_PORTS + m*BLOCK_SIZE;

                                    // read the entire row for matrix 1 from PLM into an array
                                    for (int elem_m = 0; elem_m < PLM_PORTS; elem_m++)
                                    {
                                        HLS_UNROLL_LOOP(ON, "read_plm_m");
                                        HLS_BREAK_ARRAY_DEPENDENCY(plm_in_ping);
                                        HLS_BREAK_ARRAY_DEPENDENCY(plm_in_pong);

                                        uint32_t m_index = m_offset + elem_m;

                                        if (ping)
                                            regs_m[elem_m] = plm_in_ping[m_index];
                                        else
                                            regs_m[elem_m] = plm_in_pong[m_index];
                                    }

                                    for (uint32_t n = 0; n < PLM_PORTS; n++)
                                    {
                                        HLS_PIPELINE_LOOP(HARD_STALL, 1, "pipe_mac");

                                        uint32_t n_offset = (PLM_IN_WORD/2) + n_block*BLOCK_SIZE*PLM_PORTS + k_block*PLM_PORTS + n*BLOCK_SIZE;

                                        // read the entire row for matrix 2 from PLM into an array
                                        for (int elem_n = 0; elem_n < PLM_PORTS; elem_n++)
                                        {
                                            HLS_UNROLL_LOOP(ON, "read_plm_n");
                                            HLS_BREAK_ARRAY_DEPENDENCY(plm_in_ping);
                                            HLS_BREAK_ARRAY_DEPENDENCY(plm_in_pong);

                                            uint32_t n_index = n_offset + elem_n;

                                            if (ping)
                                                regs_n[elem_n] = plm_in_ping[n_index];
                                            else
                                                regs_n[elem_n] = plm_in_pong[n_index];
                                        }

                                        // multiply all elements stored in regs_m and regs_n
                                        for (uint32_t mul = 0; mul < PLM_PORTS; mul++)
                                        {
                                            HLS_UNROLL_LOOP(ON, "multiply_k");

                                            regs_mul[mul] = regs_m[mul] * regs_n[mul];
                                        }

                                        for (uint32_t mul = 0, acc = 0; mul < 16; mul=mul+2, acc++)
                                        {
                                            HLS_UNROLL_LOOP(ON, "accumulate_k_0");

                                            regs_acc_0[acc] = regs_mul[mul]  + regs_mul[mul+1];
                                        }

                                        for (uint32_t mul = 0, acc = 0; mul < 8; mul=mul+2, acc++)
                                        {
                                            HLS_UNROLL_LOOP(ON, "accumulate_k_1");

                                            regs_acc_1[acc] = regs_acc_0[mul]  + regs_acc_0[mul+1];
                                        }

                                        for (uint32_t mul = 0, acc = 0; mul < 4; mul=mul+2, acc++)
                                        {
                                            HLS_UNROLL_LOOP(ON, "accumulate_k_2");

                                            regs_acc_2[acc] = regs_acc_1[mul]  + regs_acc_1[mul+1];
                                        }

                                        regs_acc[n] += regs_acc_2[0] + regs_acc_2[1];
                                    }

                                    // assign the accumulate to the plm_out
                                    for (int elem_n = 0; elem_n < PLM_PORTS; elem_n++)
                                    {
                                        HLS_UNROLL_LOOP(ON, "write_plm_out");
                                        HLS_BREAK_ARRAY_DEPENDENCY(plm_out_ping);
                                        HLS_BREAK_ARRAY_DEPENDENCY(plm_out_pong);

                                        uint32_t out_index = out_offset + elem_n;

                                        if (ping_out)
                                            plm_out_ping[out_index] = regs_acc[elem_n];
                                        else
                                            plm_out_pong[out_index] = regs_acc[elem_n];
                                    }
                                }
                            }
                        }
                    }
                    ping = !ping;
                }
                this->compute_store_handshake();
                ping_out = !ping_out;
            }
        }

        // Conclude
        {
            this->process_done();
        }
    }
}
