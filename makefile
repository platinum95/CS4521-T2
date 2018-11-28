CC=g++

tsxBST: tsxBST.cpp helper.h helper.cpp
	$(CC) -O3 -mrtm -mrdrnd -o tsxBST tsxBST.cpp helper.cpp -lpthread

