# GEMM accelerator

This repository contains a hardware accelerator for General Matrix Multiply (GEMM). The accelerator is written in SystemC and is synthesized to Verilog RTL using the Stratus HLS tool from Cadence. The repository can be added as a submodule in an [ESP](https://github.com/sld-columbia/esp) repository and simulated without any changes.

## Highlights of the accelerator
* We accelerate matrix multiplication by leveraging interleaving between compute and memory operations, and parallelism within the compute phase using HLS directives. The accelerator's operation is described in the following sections.
* For a 64x64 matrix multiplication, our optimized accelerator achieves a **speedup on 23x and 430x** over a naive GEMM accelerator, and a 64-bit RISC-V Ariane CPU,  respectively.
* With respect to the area overhead, our optimized accelerator **occupies only 1/6th the area** of a 64-bit RISC-V Ariane CPU.
* Our results have been validated using SystemC behavioural simulations, accelerator-level RTL simulations, full ESP system RTL simulations, and on a Xilinx VCU118 FPGA board.

## Steps to evaluate
1. Clone the ESP repository with all its submodules:
```
git clone git@github.com:sld-columbia/esp.git --recursive
```
2. Add our GEMM accelerator as a submodule:
```
git submodule add git@github.com:vsuresh95/gemm_accelerator_stratus.git accelerators/stratus_hls/gemm_accelerator_stratus
```
3. Set the required tool paths (Stratus HLS, Xcelium simulator, Vivado, etc.) and change directory to the target SoC folder:
```
cd $ESP_ROOT/socs/<target_SoC_folder>
```
4. Compile, synthesize and simulate:
```
make gemm_accelerator_stratus-sim
```
This will run a behavioural simulation, followed by HLS and then an RTL simulation.

## Accelerator operation
### Phases
The accelerator is divided into 3 phases - a load phase (to read input matrices from memory), a compute phase (to perform GEMM) and a store phase (to write the output back to memory). All 3 phases are implemented as parallel processes and handshakes are defined for communicating among themselves.

### Memory phases
* Matrices are accessed from memory using the DMA, provided in ESP by default. The DMA fetches data into the private local memories (PLM) inside the accelerator. The input PLM is configured to store 8192 integers, and the output PLM is configured to store 4096 integers.
* For matrices larger than 64x64, we employed a blocking algorithm that computes a partial GEMM of each 64x64 block in the matrix.
* To ensure that the compute phase never waits for input data to be fetched or for output data to be cleared, the PLM's are further duplicated to work in a ping-pong manner.

### Compute phase
* The compute phase performs a 64x64 GEMM by further blocking the data in input PLM into 16x16 chunks.
* We use HLS directives to leverage the inherent parallelism within the 64x64 GEMM:
  * We use loop unrolling to parallelize multiplies, accumulates and PLM accesses.
  * We sample the memory arrays into a flattened register array before the arithmetic operations
  * We pipeline all the operations across different iterations to maximize resource utilization.

## SoC design for evaluation
The image below shows the system used to evaluate the accelerator. The CPU, memory and auxiliary tiles are generated using the ESP SoC Generator.

![SoC Design with accelerator](/gemm_accelerator.png)

## Evaluation on a Xilinx VCU118 FPGA board
### Performance
| Mode of execution  | Time for 64x64 GEMM (cycles) |	Speedup |
| :-: | :-: | :-: |
| 64-bit RISC-V Ariane CPU | 15299496 | 1 |
| Naive GEMM | 813753 | 18.8 |
| **Optmized GEMM** | **35545** | **430.43** |

### Area
| Mode of execution  | CLB LUTs | CLB Registers |
| :-: | :-: | :-: |
| 64-bit RISC-V Ariane CPU | 40814 | 28283 |
| Naive GEMM | 2886	| 2393 |
| **Optmized GEMM** | **6761**	| **3371** |

## Limitations
* The accelerator only accepts matrices whose dimensions are a multiple of 64.
* The current implementation requires the second matrix in the multiply to be transposed in memory. However, the algorithm can be easily modified to accept non-transposed matrices. To do this, the load phase must be modified to fetch 64x64 blocks in column major order rather than row major order.

## Relevant links
* [Embedded Scalable Platforms (ESP)](https://esp.cs.columbia.edu): An open-source research platform for heteregeneous SoC design
