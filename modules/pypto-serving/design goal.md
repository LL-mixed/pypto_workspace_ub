I want to generate a ultra high performance inference engine that resembles the capability of a subset of vllm and sglang. 
there are following main features:

    1) written in C /C++ for ultra high performance for performance critical part of the engine.
    2) provide openai style frontend interfance that is compatible with vllm. for now provide a test path where the openai style frontend interfance is not through networking stack, but through a fast direct IPC path to inject test data into the system directly without netowkring overhead or complexity. 
    3) provide radix tree kv cache history management like sglang. for now the kv cache is stored in a and meta_data.dat (which can reside in main memory with an persistent mirror) and persistent_kv_memory_pool.dat file stored in local file system, which will be replaced by a sharded array of SSD in the future managed in fixed size blocks. 
    4) use ../simpler folder pto2 runtime to execute model. 
    5) do not allow python code in the autoregress path, the prefil to decode path, or kv cache access path, must fast language like c or c++.
    6) the whole system is long term context free (treating each conversation as new), the only persistent memory is the radix tree . 
    7) inject test data into the system through the test path.  
    8) create a copy of the golden.py from project simpler's examples. the golden.py is modifed to inject input data throught the test path, and receive the output from the test path for validation. 






