#!/bin/sh
# 
# echo "type INT"
# 
# rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestBasic.txt -t1 -q -d3 -p./
# rm mdhim.manifest
# rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestNext.txt -t1 -q -d3 -p./
# rm mdhim.manifest
# 
echo "type LONG"

rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestBasic.txt -t2 -q -d3 -p./
rm mdhim.manifest
rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestNext.txt -t2 -q -d3 -p./
rm mdhim.manifest

echo "type FLOAT"

rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestBasic.txt -t3 -q -d3 -p./
rm mdhim.manifest
rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestNext.txt -t3 -q -d3 -p./
rm mdhim.manifest

echo "type DOUBLE"

rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestBasic.txt -t4 -q -d3 -p./
rm mdhim.manifest
rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestNext.txt -t4 -q -d3 -p./
rm mdhim.manifest

# echo "type STRING"
# 
# rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestBasic.txt -t5 -q -d3 -p./
# rm mdhim.manifest
# rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestNext.txt -t5 -q -d3 -p./
# rm mdhim.manifest
# rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestLarge.txt -t5 -q -d3 -p./
# rm mdhim.manifest
# 
# echo "type BYTE"
# 
# rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestBasic.txt -t6 -q -d3 -p./
# rm mdhim.manifest
# rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestNext.txt -t6 -q -d3 -p./
# rm mdhim.manifest
# rm -rf ./mdhimTst* ; mpirun -np 2 ./mdhimtst -finTestLarge.txt -t6 -q -d3 -p./
# rm mdhim.manifest
