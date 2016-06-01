#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>

#define MSG_TAG 1000

#define STATE_DRUNK 0
#define STATE_WAITING_FOR_GROUP 1
#define STATE_FORMING_GROUP 2
#define STATE_WAITING_FOR_ARBITER 3
#define STATE_DRINKING 4

#define MSG_GROUP_REQ 100
#define MSG_GROUP_ACK 200
#define MSG_GROUP_JOIN 300
#define MSG_ARBITER_REQ 400
#define MSG_ARBITER_ACK 500

#define N_ARBITERS 10
#define GROUP_SIZE 10
#define BUFFER_SIZE 1024

#define STRUCT_SIZE 6
#define PROGRAM_TIME 1000000

long random_at_most(long max) {
  unsigned long

    num_bins = (unsigned long) max + 1,
    num_rand = (unsigned long) RAND_MAX + 1,
    bin_size = num_rand / num_bins,
    defect   = num_rand % num_bins;

  long x;
  do {
   x = random();
  }

  while (num_rand - defect <= (unsigned long)x);

  return x/bin_size;
}

int max(int num1, int num2) {
   int result;
 
   if (num1 > num2)
      result = num1;
   else
      result = num2;
 
   return result; 
}

struct Message
{
	int from;
	int type;
	int c;
	int lArbiters;
	int gId;
	int gCount;
};

int main(int argc,char **argv)
{
	MPI_Init(&argc, &argv); 
	
	MPI_Status status;
	int world_rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
	int world_size;
	MPI_Comm_size(MPI_COMM_WORLD, &world_size);

	//	MPI struct settings
	//	Remember to update definitions while changing STRUCT_SIZE
	int blocklengths[STRUCT_SIZE] = {1,1,1,1,1,1};
	MPI_Datatype types[STRUCT_SIZE] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT,MPI_INT,MPI_INT};
	MPI_Datatype drinkerMessage;
	MPI_Aint offsets[STRUCT_SIZE];

	offsets[0] = offsetof(struct Message, from);
	offsets[1] = offsetof(struct Message, type);
	offsets[2] = offsetof(struct Message, c);
	offsets[3] = offsetof(struct Message, lArbiters);
	offsets[4] = offsetof(struct Message, gId);
	offsets[5] = offsetof(struct Message, gCount);
	
	MPI_Type_create_struct(STRUCT_SIZE, blocklengths, offsets, types, &drinkerMessage);
	MPI_Type_commit(&drinkerMessage);
	//	End of MPI struct settings
	
	int clock = 0;
	int state = STATE_DRUNK;
	int wantsToDrink = 0;
	int groupId = -1;
	int groupJoins = 0;
	int ackMessages = 0;
	struct Message reqBuffer[BUFFER_SIZE];
	int lockedArbiters = 0;
	int messagesInBuffer = 0;
	int lastArbirerReqClock = 0;

	FILE * resultFile;
	char fileName[16];
	snprintf(fileName, sizeof(fileName), "result_%d.txt", world_rank);
	resultFile = fopen(fileName, "w");

	fprintf(resultFile, "%d: %d started\n", clock, world_rank);
	
	while(clock < PROGRAM_TIME)
	{
		clock++;
		if (state == STATE_DRUNK)
		{
			wantsToDrink = (random_at_most(1000) == 0);	//Zakładamy, że średni czas trzeźwienia to 1000 przebiegów pętli
			if (wantsToDrink)
			{
				fprintf(resultFile, "%d: %d wants to drink\n", clock, world_rank);
				lastArbirerReqClock = clock;
				int i;
				struct Message message;
				message.type = MSG_GROUP_REQ;
				message.c = clock;
				message.from = world_rank;
				for(i = 0; i < world_size; i++)
				{
					if (i != world_rank)
					{
						MPI_Send(&message, 1, drinkerMessage, i, MSG_TAG, MPI_COMM_WORLD);
					}
				}
				state = STATE_WAITING_FOR_GROUP;
			}
		}
		if (state == STATE_WAITING_FOR_GROUP && world_rank == 0 && groupId == -1)	//HACK PM: Dopóki nie wymyślimy, jak ogarnąć tworzenie grup, nieh tylko proces 0 je tworzy
		{
			state = STATE_FORMING_GROUP;
			groupId = world_rank;	//should be unique
			fprintf(resultFile, "%d: %d formed new group : %d\n", clock, world_rank, groupId);
			groupJoins = 1;
		}
		if (state == STATE_FORMING_GROUP)
		{
			if (groupJoins >= /*GROUP_SIZE*/ world_size)	//HACK PM: dla celów testowych - na razie liczność grupy = liczba procesów
			{
				fprintf(resultFile, "%d: %d stated that group %d is complete and sends arbiter request\n", clock, world_rank, groupId);
				state = STATE_WAITING_FOR_ARBITER;
				struct Message message;
				message.type = MSG_ARBITER_REQ;
				message.c = clock;
				message.from = world_rank;
				message.gId = groupId;
				int i;
				for (i = 0; i < world_size; i++)
				{
					if (i != world_rank)
					{
						MPI_Send(&message, 1, drinkerMessage, i, MSG_TAG, MPI_COMM_WORLD);
					}
				}
			}
		}
		if (state == STATE_WAITING_FOR_ARBITER)
		{
			if (ackMessages == world_size - 1)	//Jeżeli wszyscy nam pozwolą na dostęp
			{
				fprintf(resultFile, "%d: %d starts drinking\n", clock, world_rank);
				lockedArbiters++;
				state = STATE_DRINKING;
			}
		}
		if (state == STATE_DRINKING)
		{
			if (random_at_most(100) == 0)		//Zakładamy, że średni czas picia to 100 przebiegów pętli
			{
				fprintf(resultFile, "%d: %d finished drinking\n", clock, world_rank);
				state = STATE_DRUNK;
				lockedArbiters--;
				int i;
				for (i = 0; i < messagesInBuffer; i++)
				{
					struct Message reqMessage = reqBuffer[i];
					int from = reqMessage.from;
					struct Message message;
					message.from = world_rank;
					message.type = MSG_ARBITER_ACK;
					message.c = clock;
					message.lArbiters = lockedArbiters;
					MPI_Send(&message, 1, drinkerMessage, from, MSG_TAG, MPI_COMM_WORLD);
					//send_direct(msg{type = MSG_ARBITER_ACK, c = clock, lArbiters = lockedArbiters});
				}
				messagesInBuffer = 0;
				//reqBuffer = [];		//TODO: czyszczenie tablicy
				wantsToDrink = 0;
				groupId = -1;
				groupJoins = 0;
				ackMessages = 0;
			}
		}
		//Obsługa wiadomości:
		int isMessage = 0;
		MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &isMessage, &status );
		while(isMessage)
		{
			struct Message recvMessage;
			MPI_Recv( &recvMessage, 1, drinkerMessage, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status );
			int from = recvMessage.from;
			clock = max(clock, recvMessage.c) + 1;
			if (recvMessage.type == MSG_GROUP_REQ)
			{
				if (state == STATE_FORMING_GROUP)
				{
					struct Message message;
					message.type = MSG_GROUP_ACK;
					message.c = clock;
					message.from = world_rank;
					message.gId = groupId;
					message.gCount = groupJoins;
					MPI_Send(&message, 1, drinkerMessage, from, MSG_TAG, MPI_COMM_WORLD);
				}
			}
			if (recvMessage.type == MSG_GROUP_ACK)
			{
				if (state == STATE_WAITING_FOR_GROUP)
				{
					state = STATE_FORMING_GROUP;
					groupId = recvMessage.gId;
					groupJoins = recvMessage.gCount;
					fprintf(resultFile, "%d: %d joined group %d\n", clock, world_rank, groupId);
					struct Message message;
					message.type = MSG_GROUP_JOIN;
					message.c = clock;
					message.from = world_rank;
					message.gId = groupId;
					int i;
					for (i = 0; i < world_size; i++)
					{
						if (i != world_rank)
						{
							MPI_Send(&message, 1, drinkerMessage, i, MSG_TAG, MPI_COMM_WORLD);
						}
					}
				}
			}
			if (recvMessage.type == MSG_GROUP_JOIN)
			{
				if (state == STATE_FORMING_GROUP)
				{
					if (recvMessage.gId == groupId)
					{
						groupJoins++;
						fprintf(resultFile, "%d: %d knows that %d joined his group %d which has %d members now\n", clock, world_rank, from, groupId, groupJoins);
					}
				}
			}
			if (recvMessage.type == MSG_ARBITER_REQ)
			{
				if (state == STATE_DRUNK || state == STATE_WAITING_FOR_GROUP || state == STATE_FORMING_GROUP)
				{
					struct Message message;
					message.type = MSG_ARBITER_ACK;
					message.c = clock;
					message.lArbiters = -1;
					message.from = world_rank;
					MPI_Send(&message, 1, drinkerMessage, from, MSG_TAG, MPI_COMM_WORLD);
					//send_direct(msg{type == MSG_ARBITER_ACK, c = clock, lArbiters = -1});
				}
				else if (state == STATE_WAITING_FOR_ARBITER)
				{
					if (recvMessage.c < lastArbirerReqClock)
					{
						struct Message message;
						message.type = MSG_ARBITER_ACK;
						message.c = clock;
						message.lArbiters = -1;
						message.from = world_rank;
						MPI_Send(&message, 1, drinkerMessage, from, MSG_TAG, MPI_COMM_WORLD);
						//send_direct(msg{type == MSG_ARBITER_ACK, c = clock, lArbiters = -1});
					}
					else
					{
						reqBuffer[messagesInBuffer] = recvMessage;
						messagesInBuffer++;
					}
				}
				else	//state == STATE_DRINKING
				{
					if (lockedArbiters < N_ARBITERS)
					{
						struct Message message;
						message.type = MSG_ARBITER_ACK;
						message.c = clock;
						message.lArbiters = lockedArbiters;
						message.from = world_rank;
						MPI_Send(&message, 1, drinkerMessage, from, MSG_TAG, MPI_COMM_WORLD);
						//send_direct(msg{type == MSG_ARBITER_ACK, c = clock, lArbiters = lockedArbiters});	//Tylko procesy w tym stanie mają pełną wiedzę o liczbie zajętych arbitrów
					}
					else
					{
						reqBuffer[messagesInBuffer] = recvMessage;
						messagesInBuffer++;
					}
				}
			}
			if (recvMessage.type == MSG_ARBITER_ACK)
			{
				if (state == STATE_WAITING_FOR_ARBITER)
				{
					ackMessages++;
					lockedArbiters = max(lockedArbiters, recvMessage.lArbiters);
				}
				else if (state == STATE_DRINKING)
				{
					if (recvMessage.lArbiters > -1)
					{
						lockedArbiters = recvMessage.lArbiters;
					}
				}
			}
			MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &isMessage, &status );
		}
	}
	fprintf(resultFile, "%d: %d finished\n", clock, world_rank);
	MPI_Type_free(&drinkerMessage);
	MPI_Finalize();

	return 0;
}
