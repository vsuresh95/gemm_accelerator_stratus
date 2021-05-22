/* Copyright (c) 2011-2021 Columbia University, System Level Design Group */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <stdlib.h>

#include <esp_accelerator.h>
#include <esp_probe.h>
#include <fixed_point.h>

typedef int32_t token_t;

static unsigned DMA_WORD_PER_BEAT(unsigned _st)
{
        return (sizeof(void *) / _st);
}


#define SLD_GEMM_ACCELERATOR 0x095
#define DEV_NAME "sld,gemm_accelerator_stratus"

/* <<--params-->> */
const int32_t gemm_m = 64;
const int32_t gemm_n = 64;
const int32_t gemm_k = 64;

static unsigned in_words_adj;
static unsigned out_words_adj;
static unsigned in_len;
static unsigned out_len;
static unsigned in_size;
static unsigned out_size;
static unsigned out_offset;
static unsigned mem_size;

uint32_t checkpoint[10];

/* Size of the contiguous chunks for scatter/gather */
#define CHUNK_SHIFT 20
#define CHUNK_SIZE BIT(CHUNK_SHIFT)
#define NCHUNK(_sz) ((_sz % CHUNK_SIZE == 0) ?		\
			(_sz / CHUNK_SIZE) :		\
			(_sz / CHUNK_SIZE) + 1)

/* User defined registers */
/* <<--regs-->> */
#define GEMM_ACCELERATOR_GEMM_M_REG 0x48
#define GEMM_ACCELERATOR_GEMM_N_REG 0x44
#define GEMM_ACCELERATOR_GEMM_K_REG 0x40

static inline uint64_t get_counter()
{
	uint64_t counter;
	asm volatile (
		"li t0, 0;"
		// "rdcycle t0;"
		"csrr t0, mcycle;"
		"mv %0, t0"
		: "=r" ( counter )
		:
		: "t0"
	);

	// printf("counter: %x\n", counter);

	return counter;
}

static int validate_buf(token_t *out, token_t *gold)
{
	int i;
	int j;
	unsigned errors = 0;

	for (i = 0; i < 1; i++)
		for (j = 0; j < gemm_m * gemm_n; j++)
			if (gold[i * out_words_adj + j] != out[i * out_words_adj + j])
				errors++;

	return errors;
}


static void init_buf (token_t *in, token_t * gold)
{
	int i;
	int j;

	for (i = 0; i < 1; i++)
		for (j = 0; j < (gemm_m * gemm_k) + (gemm_n * gemm_k); j++)
			in[i * in_words_adj + j] = (token_t) (rand() % gemm_k);

	checkpoint[0] = get_counter();

	for (i = 0; i < 1; i++)
        for (int m = 0; m < gemm_m; m++)
            for (int n = 0; n < gemm_n; n++) {
                gold[i * out_words_adj + m * gemm_n + n] = 0;
                for (int k = 0; k < gemm_k; k++)
                    gold[i * out_words_adj + m * gemm_n + n] +=
                        in[i * in_words_adj + m * gemm_k + k] * in[i * in_words_adj + gemm_m * gemm_k + n * gemm_k + k];
			}

	checkpoint[1] = get_counter();
}


int main(int argc, char * argv[])
{
	int i;
	int n;
	int ndev;
	struct esp_device *espdevs;
	struct esp_device *dev;
	unsigned done;
	unsigned **ptable;
	token_t *mem;
	token_t *gold;
	unsigned errors = 0;
	unsigned coherence;

	if (DMA_WORD_PER_BEAT(sizeof(token_t)) == 0) {
		in_words_adj = (gemm_m * gemm_k) + (gemm_n * gemm_k);
		out_words_adj = gemm_m * gemm_n;
	} else {
		in_words_adj = round_up((gemm_m * gemm_k) + (gemm_n * gemm_k), DMA_WORD_PER_BEAT(sizeof(token_t)));
		out_words_adj = round_up(gemm_m * gemm_n, DMA_WORD_PER_BEAT(sizeof(token_t)));
	}
	in_len = in_words_adj * (1);
	out_len = out_words_adj * (1);
	in_size = in_len * sizeof(token_t);
	out_size = out_len * sizeof(token_t);
	out_offset  = in_len;
	mem_size = (out_offset * sizeof(token_t)) + out_size;


	// Search for the device
	printf("Scanning device tree... \n");

	ndev = probe(&espdevs, SLD_GEMM_ACCELERATOR, DEV_NAME);
	if (ndev == 0) {
		printf("gemm_accelerator not found\n");
		return 0;
	}

	for (n = 0; n < ndev; n++) {

		printf("**************** %s.%d ****************\n", DEV_NAME, n);

		dev = &espdevs[n];

		// Check DMA capabilities
		if (ioread32(dev, PT_NCHUNK_MAX_REG) == 0) {
			printf("  -> scatter-gather DMA is disabled. Abort.\n");
			return 0;
		}

		if (ioread32(dev, PT_NCHUNK_MAX_REG) < NCHUNK(mem_size)) {
			printf("  -> Not enough TLB entries available. Abort.\n");
			return 0;
		}

		// Allocate memory
		gold = aligned_malloc(out_size);
		mem = aligned_malloc(mem_size);
		printf("  memory buffer base-address = %p\n", mem);

		// Alocate and populate page table
		ptable = aligned_malloc(NCHUNK(mem_size) * sizeof(unsigned *));
		for (i = 0; i < NCHUNK(mem_size); i++)
			ptable[i] = (unsigned *) &mem[i * (CHUNK_SIZE / sizeof(token_t))];

		printf("  ptable = %p\n", ptable);
		printf("  nchunk = %lu\n", NCHUNK(mem_size));

#ifndef __riscv
		for (coherence = ACC_COH_NONE; coherence <= ACC_COH_FULL; coherence++) {
#else
		{
			/* TODO: Restore full test once ESP caches are integrated */
			coherence = ACC_COH_NONE;
#endif
			printf("  --------------------\n");
			printf("  Generate input...\n");
			init_buf(mem, gold);

			// Pass common configuration parameters

			iowrite32(dev, SELECT_REG, ioread32(dev, DEVID_REG));
			iowrite32(dev, COHERENCE_REG, coherence);

#ifndef __sparc
			iowrite32(dev, PT_ADDRESS_REG, (unsigned long long) ptable);
#else
			iowrite32(dev, PT_ADDRESS_REG, (unsigned) ptable);
#endif
			iowrite32(dev, PT_NCHUNK_REG, NCHUNK(mem_size));
			iowrite32(dev, PT_SHIFT_REG, CHUNK_SHIFT);

			// Use the following if input and output data are not allocated at the default offsets
			iowrite32(dev, SRC_OFFSET_REG, 0x0);
			iowrite32(dev, DST_OFFSET_REG, 0x0);

			// Pass accelerator-specific configuration parameters
			/* <<--regs-config-->> */
		iowrite32(dev, GEMM_ACCELERATOR_GEMM_M_REG, gemm_m);
		iowrite32(dev, GEMM_ACCELERATOR_GEMM_N_REG, gemm_n);
		iowrite32(dev, GEMM_ACCELERATOR_GEMM_K_REG, gemm_k);

			// Flush (customize coherence model here)
			esp_flush(coherence);

			// Start accelerators
			printf("  Start...\n");

			checkpoint[2] = get_counter();

			iowrite32(dev, CMD_REG, CMD_MASK_START);

			// Wait for completion
			done = 0;
			while (!done) {
				done = ioread32(dev, STATUS_REG);
				done &= STATUS_MASK_DONE;
			}
			iowrite32(dev, CMD_REG, 0x0);

			checkpoint[3] = get_counter();

			printf("  Done\n");
			printf("  validating...\n");

			/* Validation */
			errors = validate_buf(&mem[out_offset], gold);
			if (errors)
				printf("  ... FAIL\n");
			else
				printf("  ... PASS\n");
	
			printf("Time take by CPU = %d\n", checkpoint[1] - checkpoint[0]);
			printf("Time take by acc = %d\n", checkpoint[3] - checkpoint[2]);
		}
		aligned_free(ptable);
		aligned_free(mem);
		aligned_free(gold);
	}

	return 0;
}
