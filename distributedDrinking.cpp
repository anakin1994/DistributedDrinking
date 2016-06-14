#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>

#define STD_MSG_TAG 1000
#define GROUP_MSG_TAG 2000

#define STATE_DRUNK 0
#define STATE_WAITING_FOR_GROUP 1
#define STATE_FORMING_GROUP 2
#define STATE_IN_DRAFT_GROUP 3
#define STATE_CONFIRMING_GROUP 4
#define STATE_GROUP_DISBOUND 5
#define STATE_WAITING_FOR_ARBITER 6
#define STATE_DRINKING 7

#define MSG_GROUP_REQ 100

#define MSG_GROUP_JOIN_REQ 200
#define MSG_GROUP_JOIN 210
#define MSG_GROUP_JOIN_RJCT 220

#define MSG_GROUP_ANNOUNCE 300
#define MSG_GROUP_RJCT 310
#define MSG_GROUP_ACK 320

#define MSG_GROUP_CONFIRMED 400
#define MSG_GROUP_DISBOUND 410

#define MSG_ARBITER_REQ 500
#define MSG_ARBITER_ACK 510

#define MSG_TIME_UP 600


#define N_ARBITERS 10
#define GROUP_SIZE 3
#define BUFFER_SIZE 1024

#define PROGRAM_TIME 100000

#define STRUCT_SIZE 6
#define GROUP_STRUCT_SIZE 4

MPI_Datatype groupFormingMessage;
MPI_Datatype standardMessage;

struct Message
{
	int from;
	int type;
	int c;
	int lArbiters;
	int gId;
	int gCount;
};

struct GroupAnnouncement
{
	int from;
	int type;
	int setupClock;
	int groupMembers[GROUP_SIZE];
};


void setupMpiStructures()
{
	//	MPI struct settings
	//	Remember to update definitions while changing STRUCT_SIZE
	int blocklengths[STRUCT_SIZE] = {1,1,1,1,1,1};
	MPI_Datatype types[STRUCT_SIZE] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT,MPI_INT,MPI_INT};
	MPI_Aint offsets[STRUCT_SIZE];

	offsets[0] = offsetof(struct Message, from);
	offsets[1] = offsetof(struct Message, type);
	offsets[2] = offsetof(struct Message, c);
	offsets[3] = offsetof(struct Message, lArbiters);
	offsets[4] = offsetof(struct Message, gId);
	offsets[5] = offsetof(struct Message, gCount);
	
	MPI_Type_create_struct(STRUCT_SIZE, blocklengths, offsets, types, &standardMessage);
	MPI_Type_commit(&standardMessage);
	
	
	int grouMsgBlockLengths[GROUP_STRUCT_SIZE] = {1,1,1,GROUP_SIZE};
	MPI_Datatype groupMsgTypes[GROUP_STRUCT_SIZE] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT};
	
	MPI_Aint groupMsgoffsets[GROUP_STRUCT_SIZE];

	groupMsgoffsets[0] = offsetof(struct GroupAnnouncement, from);
	groupMsgoffsets[1] = offsetof(struct GroupAnnouncement, type);
	groupMsgoffsets[2] = offsetof(struct GroupAnnouncement, setupClock);
	groupMsgoffsets[3] = offsetof(struct GroupAnnouncement, groupMembers);
	
	MPI_Type_create_struct(GROUP_STRUCT_SIZE, grouMsgBlockLengths, groupMsgoffsets, groupMsgTypes, &groupFormingMessage);
	MPI_Type_commit(&groupFormingMessage);
}

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

int groupRequestsCount = 0;
bool* groupRequests;

void groupRequestUp(int position)
{
	if(!groupRequests[position])
		groupRequestsCount++;
	groupRequests[position] = true;
}

void groupRequestDown(int position)
{
	if(groupRequests[position])
		groupRequestsCount--;
	groupRequests[position] = false;
}

int main(int argc,char **argv)
{
	MPI_Init(&argc, &argv); 
	
	MPI_Status status;
	int world_rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
	int world_size;
	MPI_Comm_size(MPI_COMM_WORLD, &world_size);
	
	setupMpiStructures();
	
	groupRequests = new bool[world_size];
	int myGroup[GROUP_SIZE];
	
	for(int i = 0; i < world_size; i++)
		groupRequests[i] = 0;
	for(int i = 0; i < GROUP_SIZE; i++)
		myGroup[i] = -1;

	int myGroupClock = -1;
	int myGroupId = -1;
	int groupAckCount = 0;
	int groupJoins = 0;
	
	int clock = 0;
	int state = STATE_DRUNK;
	int wantsToDrink = 0;
	int ackMessages = 0;
	struct Message reqBuffer[BUFFER_SIZE];
	int lockedArbiters = 0;
	int messagesInBuffer = 0;
	int lastArbiterReqClock = 0;

	bool timeIsUp = false;

	FILE * resultFile;
	char fileName[16];
	snprintf(fileName, sizeof(fileName), "result_%d.txt", world_rank);
	resultFile = fopen(fileName, "w");

	fprintf(resultFile, "%d: %d started\n", clock, world_rank);
	
	while(clock < PROGRAM_TIME && !timeIsUp)
	{
		clock++;
		if (state == STATE_DRUNK)
		{
			wantsToDrink = (random_at_most(1000) == 0);	//Zakladamy, ze sredni czas trzezwienia to 1000 przebiegów petli
			if (wantsToDrink)
			{
				fprintf(resultFile, "%d: %d sobered and wants to drink. He begins looking for a group\n", clock, world_rank);
				groupRequestUp(world_rank);
				
				struct Message message;
				message.c = clock;
				message.from = world_rank;
				message.type = MSG_GROUP_REQ;
				for(int i = 0; i < world_size; i++)
				{
					if (i != world_rank)
					{
						MPI_Send(&message, 1, standardMessage, i, STD_MSG_TAG, MPI_COMM_WORLD);
					}
				}
				state = STATE_WAITING_FOR_GROUP;
			}
		}
		
		if(state == STATE_GROUP_DISBOUND)
		{
			groupRequestUp(world_rank);
				
			struct Message message;
			message.c = clock;
			message.from = world_rank;
			message.type = MSG_GROUP_REQ;
			for(int i = 0; i < world_size; i++)
			{
				if (i != world_rank)
				{
					MPI_Send(&message, 1, standardMessage, i, STD_MSG_TAG, MPI_COMM_WORLD);
				}
			}
			state = STATE_WAITING_FOR_GROUP;
			fprintf(resultFile, "%d: %d is looking for a new group\n", clock, world_rank);
		}
		
		if (state == STATE_WAITING_FOR_GROUP)
		{
			if(groupRequestsCount >= GROUP_SIZE)
			{
				struct Message joinMessage;
				joinMessage.c = clock;
				joinMessage.from = world_rank;
				joinMessage.type = MSG_GROUP_JOIN;
				joinMessage.gId = world_rank;
				
				for (int i = 0; i < world_size; i++)
				{
					if (i != world_rank)
						MPI_Send(&joinMessage, 1, standardMessage, i, STD_MSG_TAG, MPI_COMM_WORLD);
				}
				
				myGroupClock = clock;
				myGroupId = world_rank;

				fprintf(resultFile, "%d: %d formed new group : %d with ",
					clock, world_rank, myGroupId);

				struct Message message;
				message.c = clock;
				message.from = world_rank;
				message.type = MSG_GROUP_JOIN_REQ;
				message.gId = world_rank;
				int j = 0;
				for(int i = 0; i < world_size && j < GROUP_SIZE; i++)
				{
					if(groupRequests[i])
					{
						myGroup[j] = i;
						fprintf(resultFile, " %d", i);
						j++;
						if(i != world_rank)
							MPI_Send(&message, 1, standardMessage, i, STD_MSG_TAG, MPI_COMM_WORLD);
					}
				}

				fprintf(resultFile, "\n");
				
				groupJoins = 1;
				groupRequestDown(world_rank);
				
				state = STATE_FORMING_GROUP;
			}
		}
		
		if (state == STATE_FORMING_GROUP)
		{
			if (groupJoins == GROUP_SIZE)
			{
				struct GroupAnnouncement msg;
				msg.from = world_rank;
				msg.type = MSG_GROUP_ANNOUNCE;
				msg.setupClock = myGroupClock;
				for(int i = 0; i < GROUP_SIZE; i++)
					msg.groupMembers[i] = myGroup[i];
				
				for(int i = 0; i < world_size; i++)
				{
					if (i != world_rank)
						MPI_Send(&msg, 1, groupFormingMessage, i, GROUP_MSG_TAG, MPI_COMM_WORLD);
				}
				
				state = STATE_CONFIRMING_GROUP;
			}
		}
		
		if(state == STATE_CONFIRMING_GROUP)
		{
			if(groupAckCount == world_size - 1)
			{
				groupAckCount = 0;
				groupJoins = 0;
				
				struct Message message;
				message.c = clock;
				message.from = world_rank;
				message.type = MSG_GROUP_CONFIRMED;
				
				for(int i = 0; i < GROUP_SIZE; i++)
				{
					if(myGroup[i] != world_rank)
						MPI_Send(&message, 1, standardMessage, myGroup[i], STD_MSG_TAG, MPI_COMM_WORLD);
				}
				
				fprintf(resultFile, "%d: %d stated that group %d is complete and sends arbiter request\n", clock, world_rank, myGroupId);
				lastArbiterReqClock = clock;
				state = STATE_WAITING_FOR_ARBITER;
				struct Message arbMessage;
				arbMessage.type = MSG_ARBITER_REQ;
				arbMessage.c = clock;
				arbMessage.from = world_rank;
				arbMessage.gId = myGroupId;
				int i;
				for (i = 0; i < world_size; i++)
				{
					if (i != world_rank)
					{
						MPI_Send(&arbMessage, 1, standardMessage, i, STD_MSG_TAG, MPI_COMM_WORLD);
					}
				}
			}
		}
		
		if (state == STATE_WAITING_FOR_ARBITER)
		{
			if (ackMessages == world_size - 1)	//Jezeli wszyscy nam pozwola na dostep
			{
				fprintf(resultFile, "%d: %d starts drinking\n", clock, world_rank);
				lockedArbiters++;
				state = STATE_DRINKING;
			}
		}
		
		if (state == STATE_DRINKING)
		{
			if (random_at_most(100) == 0)		//Zakladamy, ze sredni czas picia to 100 przebiegów petli
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
					MPI_Send(&message, 1, standardMessage, from, STD_MSG_TAG, MPI_COMM_WORLD);
					//send_direct(msg{type = MSG_ARBITER_ACK, c = clock, lArbiters = lockedArbiters});
				}
				messagesInBuffer = 0;
				//reqBuffer = [];		//TODO: czyszczenie tablicy
				wantsToDrink = 0;
				myGroupId = -1;
				groupJoins = 0;
				ackMessages = 0;
			}
		}
		
		//Obsluga wiadomosci:
		int isStdMessage = 0;
		MPI_Iprobe(MPI_ANY_SOURCE, STD_MSG_TAG, MPI_COMM_WORLD, &isStdMessage, &status );
		while(isStdMessage)
		{
			struct Message recvMessage;
			MPI_Recv( &recvMessage, 1, standardMessage, MPI_ANY_SOURCE, STD_MSG_TAG, MPI_COMM_WORLD, &status );
			int from = recvMessage.from;
			clock = max(clock, recvMessage.c) + 1;
			
			if (recvMessage.type == MSG_GROUP_REQ)
			{
				groupRequestUp(recvMessage.from);
			}
			
			if (recvMessage.type == MSG_GROUP_JOIN_REQ)
			{
				fprintf(resultFile, "%d: %d received join request from %d, groupId: %d with group clock: %d at state: %d\n",
					clock, world_rank, from, recvMessage.gId, recvMessage.c, state);

				struct Message ackMessage;
				ackMessage.from = world_rank;
				ackMessage.c = clock;
				ackMessage.gId = recvMessage.gId;
				ackMessage.type = MSG_GROUP_JOIN;
				
				struct Message rjctMessage;
				rjctMessage.from = world_rank;
				rjctMessage.c = clock;
				rjctMessage.type = MSG_GROUP_JOIN_RJCT;
				
				if(state == STATE_WAITING_FOR_GROUP || state == STATE_GROUP_DISBOUND)
				{

					myGroupClock = recvMessage.c;
					myGroupId = recvMessage.gId;
					for (int i = 0; i < world_size; i++)
					{
						if (i != world_rank)
							MPI_Send(&ackMessage, 1, standardMessage, i, STD_MSG_TAG, MPI_COMM_WORLD);
					}

					fprintf(resultFile, "%d: %d sent join ack to request from %d \n",
						clock, world_rank, from);

					state = STATE_IN_DRAFT_GROUP;
				}
				else if (state == STATE_IN_DRAFT_GROUP)
				{
					if(myGroupClock > recvMessage.c || (myGroupClock == recvMessage.c && myGroupId > recvMessage.gId))
					{
						MPI_Send(&ackMessage, 1, standardMessage, from, STD_MSG_TAG, MPI_COMM_WORLD);

						fprintf(resultFile, "%d: %d sent join ack to request from %d because previous group (id:%d) was older (clock:%d) \n",
							clock, world_rank, from, myGroupId, myGroupClock);

						myGroupClock = recvMessage.c;
						myGroupId = recvMessage.gId;
					}
					else
					{
						fprintf(resultFile, "%d: %d sent reject to request from %d because of self group with clock: %d and id %d\n",
							clock, world_rank, from, myGroupClock, myGroupId);
						
						MPI_Send(&rjctMessage, 1, standardMessage, from, STD_MSG_TAG, MPI_COMM_WORLD);
					}
				}
				else if(state == STATE_FORMING_GROUP || state == STATE_CONFIRMING_GROUP)
				{
					if(myGroupClock > recvMessage.c || (myGroupClock == recvMessage.c && myGroupId > recvMessage.gId))
					{
						Message disboundMessage;
						disboundMessage.from = world_rank;
						disboundMessage.c = clock;
						disboundMessage.type = MSG_GROUP_DISBOUND;
						disboundMessage.gId = myGroupId;
						
						for(int i = 0;i < GROUP_SIZE; i++)
						{
							if (myGroup[i] != world_rank && myGroup[i] != from)
								MPI_Send(&disboundMessage, 1, standardMessage, myGroup[i], STD_MSG_TAG, MPI_COMM_WORLD);
							myGroup[i] = -1;
						}
						
						fprintf(resultFile, "%d: %d sent join ack to request from %d because previous group (id:%d) was older (clock:%d) \n",
							clock, world_rank, from, myGroupId, myGroupClock);

						myGroupClock = recvMessage.c;
						myGroupId = recvMessage.gId;
						MPI_Send(&ackMessage, 1, standardMessage, from, STD_MSG_TAG, MPI_COMM_WORLD);
						
						state = STATE_IN_DRAFT_GROUP;
					}
					else
					{
						fprintf(resultFile, "%d: %d sent reject to request from %d because of self group with clock: %d and id %d\n",
							clock, world_rank, from, myGroupClock, myGroupId);

						MPI_Send(&rjctMessage, 1, standardMessage, from, STD_MSG_TAG, MPI_COMM_WORLD);
					}
				}
			}
			
			if (recvMessage.type == MSG_GROUP_JOIN)
			{
				groupRequestDown(from);
				/*fprintf(resultFile, "%d: %d knows that %d joined group %d\n",
					clock, world_rank, from, recvMessage.gId);*/
				
				if(state == STATE_FORMING_GROUP)
				{
					if (recvMessage.gId == myGroupId)
					{
						groupJoins++;
						fprintf(resultFile, "%d: %d knows that %d joined his group %d which has %d members now\n",
							clock, world_rank, from, myGroupId, groupJoins);
					}
				}				
			}
			
			if(recvMessage.type == MSG_GROUP_JOIN_RJCT)
			{
				if(state == STATE_FORMING_GROUP)
				{
					//TODO upewnienie sie czy ziomek jest w mojej grupie
					Message disboundMessage;
					disboundMessage.from = world_rank;
					disboundMessage.c = clock;
					disboundMessage.type = MSG_GROUP_DISBOUND;
					disboundMessage.gId = myGroupId;
					
					for(int i = 0;i < GROUP_SIZE; i++)
					{
						if (myGroup[i] != world_rank && myGroup[i] != from)
							MPI_Send(&disboundMessage, 1, standardMessage, myGroup[i], STD_MSG_TAG, MPI_COMM_WORLD);
						myGroup[i] = -1;
					}
					
					groupAckCount = 0;
					groupJoins = 0;
					
					state = STATE_GROUP_DISBOUND;
				}
			}
			
			if(recvMessage.type == MSG_GROUP_DISBOUND)
			{
				if(myGroupId == recvMessage.gId)
					state = STATE_GROUP_DISBOUND;
			}
			
			if(recvMessage.type == MSG_GROUP_ACK)
			{
				//TODO check if im right to get this message
				groupAckCount++;
				fprintf(resultFile, "%d: %d received group ack from %d and has now %d group ack's\n",
					clock, world_rank, from, groupAckCount);
			}

			if (recvMessage.type == MSG_GROUP_RJCT)
			{
				fprintf(resultFile, "%d: %d received group rjct from %d and disbounds his group\n",
					clock, world_rank, from);

				if (state == STATE_CONFIRMING_GROUP)
				{
					Message disboundMessage;
					disboundMessage.from = world_rank;
					disboundMessage.c = clock;
					disboundMessage.type = MSG_GROUP_DISBOUND;
					disboundMessage.gId = myGroupId;

					for (int i = 0;i < GROUP_SIZE; i++)
					{
						if (myGroup[i] != world_rank && myGroup[i] != from)
							MPI_Send(&disboundMessage, 1, standardMessage, myGroup[i], STD_MSG_TAG, MPI_COMM_WORLD);
						myGroup[i] = -1;
					}

					groupAckCount = 0;
					groupJoins = 0;

					state = STATE_GROUP_DISBOUND;
				}
			}
			
			if(recvMessage.type == MSG_GROUP_CONFIRMED)
			{
				//TODO check if im right to get this message
				fprintf(resultFile, "%d: %d received info that group %d is complete and sends arbiter request\n", clock, world_rank, myGroupId);
				lastArbiterReqClock = clock;
				state = STATE_WAITING_FOR_ARBITER;
				struct Message arbMessage;
				arbMessage.type = MSG_ARBITER_REQ;
				arbMessage.c = clock;
				arbMessage.from = world_rank;
				arbMessage.gId = myGroupId;
				int i;
				for (i = 0; i < world_size; i++)
				{
					if (i != world_rank)
					{
						MPI_Send(&arbMessage, 1, standardMessage, i, STD_MSG_TAG, MPI_COMM_WORLD);
					}
				}
			}
			
			if (recvMessage.type == MSG_ARBITER_REQ)
			{
				if (state == STATE_WAITING_FOR_ARBITER)
				{
					if (recvMessage.c < lastArbiterReqClock || recvMessage.gId == myGroupId || (recvMessage.c == lastArbiterReqClock && from < world_rank))
					{
						struct Message message;
						message.type = MSG_ARBITER_ACK;
						message.c = clock;
						message.lArbiters = -1;
						message.from = world_rank;
						MPI_Send(&message, 1, standardMessage, from, STD_MSG_TAG, MPI_COMM_WORLD);
						//send_direct(msg{type == MSG_ARBITER_ACK, c = clock, lArbiters = -1});
					}
					else
					{
						reqBuffer[messagesInBuffer] = recvMessage;
						messagesInBuffer++;
					}
				}
				else if(state == STATE_DRINKING)
				{
					if (lockedArbiters < N_ARBITERS)
					{
						struct Message message;
						message.type = MSG_ARBITER_ACK;
						message.c = clock;
						message.lArbiters = lockedArbiters;
						message.from = world_rank;
						MPI_Send(&message, 1, standardMessage, from, STD_MSG_TAG, MPI_COMM_WORLD);
						//send_direct(msg{type == MSG_ARBITER_ACK, c = clock, lArbiters = lockedArbiters});	//Tylko procesy w tym stanie maja pelna wiedze o liczbie zajetych arbitrów
					}
					else
					{
						reqBuffer[messagesInBuffer] = recvMessage;
						messagesInBuffer++;
					}
				}
				else
				{
					struct Message message;
					message.type = MSG_ARBITER_ACK;
					message.c = clock;
					message.lArbiters = -1;
					message.from = world_rank;
					MPI_Send(&message, 1, standardMessage, from, STD_MSG_TAG, MPI_COMM_WORLD);
					//send_direct(msg{type == MSG_ARBITER_ACK, c = clock, lArbiters = -1});
				}
			}
			if (recvMessage.type == MSG_ARBITER_ACK)
			{
				if (state == STATE_WAITING_FOR_ARBITER)
				{
					ackMessages++;
					fprintf(resultFile, "%d: %d received arbiter ACK from %d and has %d ACKs now\n", clock, world_rank, from, ackMessages);
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
			if (recvMessage.type == MSG_TIME_UP)
			{
				timeIsUp = true;
				fprintf(resultFile, "%d: %d received end message from %d\n", clock, world_rank, from);
			}
			MPI_Iprobe(MPI_ANY_SOURCE, STD_MSG_TAG, MPI_COMM_WORLD, &isStdMessage, &status );
		}
		
		int isGroupAnnouncement = 0;
		MPI_Iprobe(MPI_ANY_SOURCE, GROUP_MSG_TAG, MPI_COMM_WORLD, &isGroupAnnouncement, &status );
		while(isGroupAnnouncement)
		{
			GroupAnnouncement recvMessage;
			MPI_Recv(&recvMessage, 1, groupFormingMessage, MPI_ANY_SOURCE, GROUP_MSG_TAG, MPI_COMM_WORLD, &status);
			if(recvMessage.type == MSG_GROUP_ANNOUNCE)
			{
				struct Message resMsg;
				resMsg.c = clock;
				resMsg.from = world_rank;
					
				if(state == STATE_FORMING_GROUP || state == STATE_CONFIRMING_GROUP)
				{
					bool intersects = false;
					for(int i = 0; i < GROUP_SIZE; i++)
					{
						for(int j = 0; j < GROUP_SIZE; j++)
						{
							if(recvMessage.groupMembers[i] == myGroup[j])
								intersects = true;
						}
					}
					
					if(intersects)
					{		
						if(myGroupClock < recvMessage.setupClock)
						{
							resMsg.type = MSG_GROUP_RJCT;
							MPI_Send(&resMsg, 1, standardMessage, recvMessage.from, STD_MSG_TAG, MPI_COMM_WORLD);
						}
						else
						{
							resMsg.type = MSG_GROUP_ACK;
							MPI_Send(&resMsg, 1, standardMessage, recvMessage.from, STD_MSG_TAG, MPI_COMM_WORLD);
							
							Message disboundMessage;
							disboundMessage.from = world_rank;
							disboundMessage.c = clock;
							disboundMessage.type = MSG_GROUP_DISBOUND;
							disboundMessage.gId = myGroupId;
							
							for(int i = 0;i < GROUP_SIZE; i++)
							{
								if (myGroup[i] != world_rank && myGroup[i] != recvMessage.from)
									MPI_Send(&disboundMessage, 1, standardMessage, myGroup[i], STD_MSG_TAG, MPI_COMM_WORLD);
								myGroup[i] = -1;
							}
							
							groupAckCount = 0;
							groupJoins = 0;
							state = STATE_GROUP_DISBOUND;
						}
					}
					else
					{
						resMsg.type = MSG_GROUP_ACK;
						MPI_Send(&resMsg, 1, standardMessage, recvMessage.from, STD_MSG_TAG, MPI_COMM_WORLD);
					}
				}
				else
				{
					resMsg.type = MSG_GROUP_ACK;
					MPI_Send(&resMsg, 1, standardMessage, recvMessage.from, STD_MSG_TAG, MPI_COMM_WORLD);
				}
			}
			
			MPI_Iprobe(MPI_ANY_SOURCE, GROUP_MSG_TAG, MPI_COMM_WORLD, &isGroupAnnouncement, &status);
		}
	}

	Message endMessage;
	endMessage.from = world_rank;
	endMessage.c = -1;	//Done this way to differ clock difference between threads from deadlock
	endMessage.type = MSG_TIME_UP;
	for (int i = 0; i < world_size; i++)
	{
		if (i != world_rank)
		{
			MPI_Send(&endMessage, 1, standardMessage, i, STD_MSG_TAG, MPI_COMM_WORLD);
		}
	}
	
	fprintf(resultFile, "%d: %d finished\n", clock, world_rank);
	MPI_Type_free(&standardMessage);
	MPI_Type_free(&groupFormingMessage);
	MPI_Finalize();

	return 0;
}