Method to port a AI operator/model to pypto v3. 
1) understand pypto v3's programming style define in pypto-frotend-coding-style.
2) understand how pypto v3's orchestartion function + incore functions are executed in a runtime environment defined in pot2_rt.md. 
3) each program to be ported is put to a subfolder under projects folder. the subfolder contains the program to be ported, and create pypto_src subfolder to save the generated target golden.py, source code for orchestrtion function, and source code for incore function. 
4) create the necessay Claude skills to be saved in pypto-lib for porting. 
5) Use ../pypto containing the tools to complie the generated code, you need to check if the generated code can be compiled without error. If there is error, you must fix the grammar error. 
6) In generating the incore functions, the tensor operations must use pto iSa instructions, the tensors must be adapted to tiles using TLOAD and tile results to be written back to tensor using TSTORE. 
7) consective tile operations that forms producer consumer relationship should be group together into the same incore function, to achieved maximum benefit of fusion. 
8) compilation will generate SRAM buffer usage statistics or error (if it encounters Out of SRAM memory error). If the SRAM is undertilized, you can try to group more tile operations into the same incore kernel. If the memory usage exceeds buffer limit, the some tile operations need to be moved out of this incore function.
9) when writing the orcehstartion and in core functions, to provide more opportunity for fusion inside incore function, a large tensor operation or a large outer loop can be divided into smaller chunks such that the operations on chunks (of sub tensor) have opportunity to be fused across consecutive loop bodies. you must try to maximize the chance of fusing several consective operations together by properly organizing the orchestration functions's loop, by interchanging inner and outer loop, by converting a single layer of loop of multiple layers of loop via tilng and a combination of such measures. loops optimization/transformations, especially of parallel loops can greatly benefit fusion, you need to maximize the benefit of loop interchange. 
10) tiling size can be expanded or shrink to fit the SRAM buffer limits of in core functions.
11) leverage ideas and lessons learnt from flash attention, and variants of flash attention to try to rewrite the code to achieve similar benefits. 




