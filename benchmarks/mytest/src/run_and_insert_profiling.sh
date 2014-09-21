clang -emit-llvm -o $1.bc -c $1.c
opt -insert-edge-profiling $1.bc -o $1.profile.bc
llc $1.profile.bc -o $1.profile.s
g++ -o $1.profile $1.profile.s /home/mark/llvm_dir/build/Release+Asserts/lib/libprofile_rt.so
./$1.profile
