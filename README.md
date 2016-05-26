# DistributedDrinking
Application simulates drinking competition with limited number of arbiters using distributed architecture and MPI librarby.

Results are saved to results.txt.
To view results use: sort -n results.txt | more
It's important because fprinf function can use a buffer and write lines in time-inconsistent order.
First number in every line indicates the Lamport's clock value.
