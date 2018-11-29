CC=g++

tsxBST: tsxBST.cpp helper.h helper.cpp gtsxBST
	$(CC) -O3 -mrtm -mrdrnd -o tsxBST tsxBST.cpp helper.cpp -lpthread
                                                                                                                                                                                          
gtsxBST: tsxBST.cpp helper.h helper.cpp                                                                                                                                                                        
	$(CC) -g -O0 -mrtm -mrdrnd -o gtsxBST tsxBST.cpp helper.cpp -lpthread 
