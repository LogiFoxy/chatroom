#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define STR_SIZE 32
#define MAX_CONTACTS 32
#define MAX_GROUPS 10
#define MAX_CLIENTS_PER_GROUP 10

static _Atomic unsigned int cli_count = 0;
static _Atomic unsigned int group_count = 0;
static int uid = 10;

static const char USERNAME_ERROR[] = "Username already exists.\n";
static const char REGISTER_SUCCESS[] = "Registered successfully.\n";
static const char LOGIN_ERROR[] = "Log in failed.\n";
static const char LOGIN_SUCCESS[] = "Logged in successfully.\n";
static const char GROUP_ERROR[] = "No valid group names found.\n";

static const char REGISTER[] = "R";
static const char LOGIN[] = "L";

static const char CREATE_GROUP[] = "cgroup";
static const char DELETE_GROUP[] = "dgroup";
static const char ENTER_GROUP[] = "egroup";
static const char SHOW_GROUPS[] = "sgroups";
static const char ADD_CONTACT[] = "acontact";
static const char DELETE_CONTACT[] = "dcontact";
static const char CONTACT_LIST[] = "clist";
static const char PERSONAL_MESSAGE[] = "pm ";
static const char GROUP_MESSAGE[] = "mgroup";

/* Client structure */
typedef struct{
	struct sockaddr_in address;
	int sockfd;
	int uid;
	char name[STR_SIZE];
	char contacts[MAX_CONTACTS][STR_SIZE];
} client_t;

/* Group structure */
typedef struct{
	char name[STR_SIZE];
	char admin[STR_SIZE];
	client_t users[MAX_CLIENTS_PER_GROUP];
} group_t;

client_t *clients[MAX_CLIENTS];
group_t *groups[MAX_GROUPS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* trim \n */
void str_trim_lf (char* arr, int length) {
  int i;
  for (i = 0; i < length; i++) {
    if (arr[i] == '\n') {
      arr[i] = '\0';
      break;
    }
  }
}

void trim_leading(char * str) {
    int index, i, j;
    index = 0;

    /* Find last index of whitespace character */
    while(str[index] == ' ' || str[index] == '\t' || str[index] == '\n') {
        index++;
    }

    if(index != 0) {
        /* Shift all trailing characters to its left */
        i = 0;
        while(str[i + index] != '\0') {
            str[i] = str[i + index];
            i++;
        }
        str[i] = '\0'; // Make sure that string is NULL terminated
    }
}

void substring(char s[], char sub[], int pos, int length) {
   int i = 0;

   while (i < length) {
      sub[i] = s[pos+i-1];
      i++;
   }
   sub[i] = '\0';
}

int search_in_file(char *fname, char *str, int ignore) {
	FILE *fp;
	int line_num = 1;
	int find_result = 0;
	char temp[512];

	if((fp = fopen(fname, "r")) == NULL) {
		perror(fname);
		return(-1);
	}

	while(fgets(temp, 512, fp) != NULL) {
		if(ignore==1 && strncmp(temp,"contacts",8)==0) {
			line_num++;
			continue;
		}

		if((strstr(temp, str)) != NULL) {
			find_result++;
			return line_num;
		}
		line_num++;
	}

	if(find_result == 0) {
		return (-1);
	}

	if(fp) {
		fclose(fp);
	}
   	return(-1);
}

int contact_exists(char *contact_name, client_t *cl) {
	int result = -1; // Contact not found

	for(int i=0; i<sizeof cl->contacts; i++) {
		if(strcmp(cl->contacts[i],contact_name) == 0){
			result = 0; // Contact found
		}
	}

	return result;
}

void print_client_addr(struct sockaddr_in addr){
    printf("%d.%d.%d.%d",
        addr.sin_addr.s_addr & 0xff,
        (addr.sin_addr.s_addr & 0xff00) >> 8,
        (addr.sin_addr.s_addr & 0xff0000) >> 16,
        (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

/* Add clients to queue */
void queue_add(client_t *cl){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i < MAX_CLIENTS; ++i){
		if(!clients[i]){
			clients[i] = cl;
			break;
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}

/* Remove clients from queue */
void queue_remove(int uid){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i < MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->uid == uid){
				clients[i] = NULL;
				break;
			}
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}

/* Add groups to queue */
void queue_add_group(group_t *gr){

	for(int i=0; i < MAX_GROUPS; ++i){
		if(!groups[i]){
			groups[i] = gr;
			break;
		}
	}
}

/* Remove clients from queue */
void queue_remove_group(char *group_name){

	for(int i=0; i < MAX_GROUPS; ++i){
		if(groups[i]){
			if(strcmp(groups[i]->name,group_name) == 0){
				groups[i] = NULL;
				break;
			}
		}
	}

}

/* Add clients to group */
int add_to_group(client_t *cl, char *group_name){

	int added = -1; // group_name not found in groups
	str_trim_lf(group_name,strlen(group_name));

	for(int i=0; i < group_count; ++i){
		if(strcmp(groups[i]->name,group_name)==0) {
			added = 0; // group_name found in groups
			for(int j=0; j < MAX_CLIENTS_PER_GROUP; ++j) {
				if(strcmp(groups[i]->users[j].name,cl->name) == 0) {
					added = -2; // client already in group
					goto DONE;
				}
				if(strcmp(groups[i]->users[j].name,"\0") == 0) {
					strcpy(groups[i]->users[j].name,cl->name);
					groups[i]->users[j].sockfd = cl->sockfd;
					groups[i]->users[j].uid = cl->uid;
					added = 1; // client added to group
					goto DONE;
				}
			}
		}
	}

	DONE:
	return added;
}

/* Send a personal message to a contact */
int send_pm(char *s, char *contact_name, client_t *cl){
	pthread_mutex_lock(&clients_mutex);

	int result = contact_exists(contact_name,cl);

	if(result == 0) {
		for(int i=0; i<MAX_CLIENTS; ++i){
			if(clients[i]){
				if(strcmp(clients[i]->name,contact_name) == 0){
					char buffer[BUFFER_SZ];
					sprintf(buffer,"[PM]%s: %s\n", cl->name, s);
					if(write(clients[i]->sockfd, buffer, strlen(buffer)) < 0){
						result = -1; // message not sent
					}
					result = 1; // message sent
					break;
				}
			}
		}
	}

	pthread_mutex_unlock(&clients_mutex);
	return result;
}

/* Send group message */
int send_gm(char *message, char *group_name, client_t *cl){
	pthread_mutex_lock(&clients_mutex);

	int result = -1; // Group name not found in groups
	int u_found_in_group = -1; // User not found in group

	char buffer[BUFFER_SZ];
	sprintf(buffer,"[%s]%s: %s\n",group_name, cl->name, message);

	for(int i=0; i<group_count; i++) {
		if(strcmp(groups[i]->name,group_name) == 0){
			result = 0; // Group name found in groups
			for(int j=0;j<MAX_CLIENTS_PER_GROUP;j++){
				if(strcmp(groups[i]->users[j].name,cl->name)==0) {
					u_found_in_group = 0; // User found in group
				}
			}
			if(u_found_in_group == 0) {
				for(int j=0;j<MAX_CLIENTS_PER_GROUP;j++){
					if(strcmp(groups[i]->users[j].name,cl->name)!=0 && strcmp(groups[i]->users[j].name,"\0")!=0) {
						printf("Writing message to user %s with sockfd %d\n",groups[i]->users[j].name,groups[i]->users[j].sockfd );
						if(write(groups[i]->users[j].sockfd, buffer, strlen(buffer)) < 0){
							result = -1; // Message not sent to groups[i]->users[j].sockfd
						}
						result = 1; // Message sent to groups[i]->users[j].sockfd
					}
				}
			} else {
				result = -2; // User not found in group
			}
		}
		if(result==0) {
			break;
		}
	}

	pthread_mutex_unlock(&clients_mutex);
	return result;
}

/* Send message to all clients except sender */
void send_message(char *s, int uid){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i<MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->uid != uid){
				if(write(clients[i]->sockfd, s, strlen(s)) < 0){
					perror("ERROR: write to descriptor failed");
					break;
				}
			}
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}

/* Handle all communication with the client */
void *handle_client(void *arg){
	char buff_out[BUFFER_SZ];
	char buffer[BUFFER_SZ];
	char name[STR_SIZE];
	char pswd[STR_SIZE];
	char action[STR_SIZE];
	char groups_input[1024];
	char contact_name[STR_SIZE];
	int leave_flag = 0;
	int user_line_found = -1;
	int user_complete = 1;
	FILE *file;
	FILE *temp;
	FILE *groups_file;

	char *line = NULL;
  size_t len = 0;
  ssize_t read;

	cli_count++;
	client_t *cli = (client_t *)arg;

	// Check if Register or Login
	if(recv(cli->sockfd, action, STR_SIZE, 0) <= 0 || strlen(action) >= STR_SIZE-1){
		printf("Wrong action input.\n");
		leave_flag = 1;
	} else if(strcmp(action,REGISTER)==0){

		file=fopen("users.txt","a+");
		if(file==NULL) {
			perror("Error opening file users.txt.\n");
			leave_flag = 1;
		}

		if(recv(cli->sockfd, name, STR_SIZE, 0) <= 0 || strlen(name) <  2 || strlen(name) >= STR_SIZE-1){
			printf("Didn't enter the name.\n");
			leave_flag = 1;
		} else{

			sprintf(buffer,"%s",name);
			user_line_found = search_in_file("users.txt",buffer,1);

			if(user_line_found > 0) {
				printf("Username already exists. Disconnecting...\n");

				send(cli->sockfd, USERNAME_ERROR, STR_SIZE, 0);
				fclose(file);
				goto EXIT;
			}

			if(leave_flag != 1) {
				strcpy(cli->name, name);
				sprintf(buff_out, "%s registering now\n", cli->name);
				printf("%s", buff_out);

				// Password
				if(recv(cli->sockfd, pswd, STR_SIZE, 0) <= 0 || strlen(pswd) <  2 || strlen(pswd) >= STR_SIZE-1){
					printf("Didn't enter the password.\n");
					goto EXIT;
				}

				bzero(buffer,BUFFER_SZ);

				// Ask user to join groups
				int i;
				for(i=0;i<group_count;i++) {
					sprintf(buffer + strlen(buffer), "%d. %s\n", i+1, groups[i]->name);
					//write(cli->sockfd, buffer, strlen(buffer));
				}
				printf("%s\n", buffer);
				send(cli->sockfd, buffer, BUFFER_SZ, 0);

				// groups
				if(recv(cli->sockfd, groups_input, 1024, 0) <= 0 || strlen(groups_input) <  2 || strlen(groups_input) >= 1024-1){
					printf("Didn't enter the groups.\n");
					goto EXIT;
				}

				printf("Groups entered: %s\n",groups_input);

				str_trim_lf(groups_input,strlen(groups_input));
				trim_leading(groups_input);
				char *pointer=strtok(groups_input,",");
				char groups_not_found[MAX_GROUPS][STR_SIZE];
				char groups_found[MAX_GROUPS][STR_SIZE];
				int nf=0;
				int f=0;

				while (pointer != NULL) {
					int group_found = add_to_group(cli,pointer);

						if(group_found == -1) {
							strcpy(groups_not_found[nf],pointer);
							nf++;
						} else {
							strcpy(groups_found[f],pointer);
							f++;
						}
						pointer = strtok (NULL, ",");
				}

				// No valid group names to join found
				if(f-1 < 0) {
					printf(GROUP_ERROR);
					send(cli->sockfd, GROUP_ERROR, BUFFER_SZ, 0);
					goto EXIT;
				}

				printf("Saving username...\n");
				fputs(name,file);
				fputs(":",file);
				printf("Saving password...\n");
				fputs(pswd,file);
				fputs("\n",file);
				fputs("contacts:\n",file);
				fputs("groups:",file);
				for(int k=0;k<sizeof(groups_found);k++) {
					if(strcmp(groups_found[k],"\0")==0) {
						break;
					} else {
						fputs(":",file);
						fputs(groups_found[k],file);
					}
				}
				fputs("\n",file);

				bzero(buffer,BUFFER_SZ);
				sprintf(buffer+strlen(buffer),"%s", REGISTER_SUCCESS);

				if(nf-1 >= 0) {
					sprintf(buffer+strlen(buffer),"Groups not joined:");
					for(int k=0;k<sizeof(groups_not_found);k++) {
						if(strcmp(groups_not_found[k],"\0")==0) {
							break;
						} else {
							sprintf(buffer+strlen(buffer)," %s ", groups_not_found[k]);
						}
					}
				}

				send(cli->sockfd, buffer, BUFFER_SZ, 0);

			}
			bzero(buffer,BUFFER_SZ);

		}

		fclose(file);

	} else if(strcmp(action,LOGIN)==0) {
		// Login
		printf("Logging in...\n");

		// Name
		if(recv(cli->sockfd, name, STR_SIZE, 0) <= 0 || strlen(name) <  2 || strlen(name) >= STR_SIZE-1){
			printf("Didn't enter the name.\n");
			leave_flag = 1;
		}
		strcpy(cli->name, name);

		// Password
		if(recv(cli->sockfd, pswd, STR_SIZE, 0) <= 0 || strlen(pswd) <  2 || strlen(pswd) >= STR_SIZE-1){
			printf("Didn't enter the password.\n");
			leave_flag = 1;
		}

		if(leave_flag != 1) {

			char buff[BUFFER_SZ];

			sprintf(buff,"%s:%s",name,pswd);
			user_line_found = search_in_file("users.txt",buff,1);

			if(user_line_found > 0) {
				file = fopen("users.txt","r+");
				if(file==NULL) {
					perror("Error opening users.txt\n");
				}

				int line_index = 1;

				while ((read = getline(&line, &len, file)) != -1) {

					// Checking line after user found for contacts
					if(line_index == user_line_found+1 || line_index == user_line_found+2) {
						char temp_buff[BUFFER_SZ];
						strcpy(temp_buff,line);
						char *p=strtok(temp_buff,":");
						int j = 0;
						if(strncmp(temp_buff,"contacts",8) == 0) {

					    while (p != NULL) {
									p = strtok (NULL, ":");
									if(p != NULL && strcmp(p,"\0") != 0 && strcmp(p,"\n") != 0){
										str_trim_lf(p,strlen(p));
										printf("Adding contact %s to user.\n",p );
										strcpy(cli->contacts[j],p);
									}
									j++;
					    }
						}
						else if(strncmp(temp_buff,"groups",6) == 0) {
							while (p != NULL) {
									 p = strtok (NULL, ":");
									 if(p!=NULL) {
										 int added = add_to_group(cli, p);
									 }

							 }
						 }
					}
					line_index++;
				}

				fclose(file);

				strcpy(cli->name, name);
				sprintf(buff_out, "User %s logged in\n", cli->name);
				printf("%s", buff_out);
				send(cli->sockfd, LOGIN_SUCCESS, STR_SIZE, 0);
			} else {
				printf("User not found.\n");
				send(cli->sockfd, LOGIN_ERROR, STR_SIZE, 0);
				goto EXIT;
			}

		}

	}

	bzero(buff_out, BUFFER_SZ);

	while(1){
		if (leave_flag) {
			break;
		}

		int receive = recv(cli->sockfd, buff_out, BUFFER_SZ, 0);
		if(strncmp(buff_out,CREATE_GROUP,strlen(CREATE_GROUP)) == 0) {

			char group_name[STR_SIZE];
			substring(buff_out, group_name, strlen(CREATE_GROUP)+1, strlen(buff_out));
			trim_leading(group_name);
			str_trim_lf(group_name,strlen(group_name));

			bzero(buffer,BUFFER_SZ);
			sprintf(buffer,"%s:%s",group_name,cli->name);
			int group_line_num = search_in_file("groups.txt",buffer,0);

			if(group_line_num == -1) {
				groups_file = fopen("groups.txt","a+");
				if(groups_file==NULL) {
					perror("Error opening groups.txt\n");
				}

				bzero(buffer, BUFFER_SZ);
				sprintf(buffer,"%s:%s\n",group_name,cli->name);
				fputs(buffer,groups_file);

				group_t *gr = (group_t *)malloc(sizeof(group_t));
				strcpy(gr->name,group_name);
				strcpy(gr->admin,cli->name);
				queue_add_group(gr);
				group_count++;

				fclose(groups_file);

				write(cli->sockfd,"Group successfully created.You are its admin, but not yet a member.\n",
				strlen("Group successfully created.You are its admin.You are its admin, but not yet a member.\n"));
			} else {
				write(cli->sockfd,"Group not created.Duplicate name-admin combo.\n",
				strlen("Group successfully created.Duplicate name-admin combo.\n"));
			}

		}	else if(strncmp(buff_out,DELETE_GROUP,strlen(DELETE_GROUP)) == 0) {
			char group_name[STR_SIZE];
			substring(buff_out, group_name, strlen(DELETE_GROUP)+1, strlen(buff_out));
			trim_leading(group_name);
			str_trim_lf(group_name,strlen(group_name));

			int deleted = -1;

			for(int i=0;i<group_count;i++) {
				if(strcmp(groups[i]->name,group_name)==0 && strcmp(groups[i]->admin,cli->name)==0) {
					queue_remove_group(group_name);
					deleted = 0;
					bzero(buffer,BUFFER_SZ);
					sprintf(buffer,"%s:%s",group_name,cli->name);
					group_count--;
					break;
				}
			}

			bzero(buffer,BUFFER_SZ);

			file = fopen("users.txt","r");
			temp=fopen("temp.txt","a+");
			if(file==NULL || temp==NULL) {
				perror("Error opening file.\n");
			}

			while ((read = getline(&line, &len, file)) != -1) {
				if(strncmp(line,"groups",6)!=0) {
					fputs(line,temp);
				} else {
					char temp_buff[BUFFER_SZ];
					char new_line[BUFFER_SZ];
					strcpy(temp_buff,line);
					str_trim_lf(temp_buff,strlen(temp_buff));
					char *p=strtok(temp_buff,":");

					strcpy(new_line,"groups:");

					while (p != NULL) {
							p = strtok (NULL, ":");
							if(p != NULL && strcmp(p,"\0") != 0 && strcmp(p,"\n") != 0
								&& strncmp(p,group_name,strlen(group_name))!=0 ){
								sprintf(new_line + strlen(new_line),":%s",p);
							}
					}

					fputs(new_line,temp);
					fputs("\n",temp);
				}
			}

			remove("users.txt");
			rename("temp.txt", "users.txt");

			fclose(temp);
			fclose(file);

			if(deleted == 0) {
				int group_line_num = search_in_file("groups.txt",buffer,0);
				int line_num = 1;

				groups_file = fopen("groups.txt","r");
				temp=fopen("temp.txt","a+");
				if(groups_file==NULL || temp==NULL) {
					perror("Error opening file.\n");
					deleted = -1;
				}

				while ((read = getline(&line, &len, groups_file)) != -1) {
					if(line_num != group_line_num) {
						fputs(line,temp);
					}
					line_num++;
				}

				remove("groups.txt");
				rename("temp.txt", "groups.txt");

				fclose(temp);
				fclose(groups_file);

				deleted = 1;
			}

			if(deleted == -1) {
				write(cli->sockfd,"Group not deleted.Wrong group name or user is not admin.\n",
				strlen("Group not deleted.Wrong group name or user is not admin.\n"));
			}
			else if(deleted == 0) {
				write(cli->sockfd,"Group not deleted.Unknown error.\n",
				strlen("Group not deleted.Unknown error.\n"));
			} else {
				write(cli->sockfd,"Group successfully deleted.\n",
				strlen("Group successfully deleted.\n"));
			}

		} else if(strncmp(buff_out,ENTER_GROUP, strlen(ENTER_GROUP)) == 0) {
			char group_enter[STR_SIZE];

			substring(buff_out, group_enter, strlen(ENTER_GROUP)+1, strlen(buff_out));
			trim_leading(group_enter);
			str_trim_lf(group_enter,strlen(group_enter));

			int added = add_to_group(cli,group_enter);

			if(added == 1) {
				bzero(buffer,BUFFER_SZ);
				sprintf(buffer,"%s:%s",cli->name,pswd);
				int user_start_line = search_in_file("users.txt",buffer,1);
				int line_index = 1;

				file=fopen("users.txt","r");
				if(file==NULL) {
					perror("Error opening users.txt\n");
				}
				temp=fopen("temp.txt","a+");
				if(file==NULL) {
					perror("Error opening temp.txt\n");
				}

				while ((read = getline(&line, &len, file)) != -1) {
					if(line_index != user_start_line +2) {
						fputs(line,temp);
					} else {
						bzero(buffer,BUFFER_SZ);
						str_trim_lf(line,strlen(line));
						strcpy(buffer,line);
						sprintf(buffer + strlen(buffer),":%s\n",group_enter);
						fputs(buffer,temp);
					}
					line_index++;
				}

				fclose(file);
				fclose(temp);

				remove("users.txt");
				rename("temp.txt", "users.txt");
			}

			if(added == -2) {
				write(cli->sockfd,"You are already a member.\n",strlen("You are already a member.\n"));
			} else if(added == -1) {
				write(cli->sockfd,"Group name not found.\n",strlen("Group name not found.\n"));
			} else if(added == 0) {
				write(cli->sockfd,"Group not entered.Unknown error.\n",
				strlen("Group not entered.Unknown error.\n"));
			} else {
				write(cli->sockfd,"Entered group successfully.\n",strlen("Entered group successfully.\n"));
			}

		} else if(strcmp(buff_out,SHOW_GROUPS) == 0) {

			write(cli->sockfd, "Groups List:\n", strlen("Groups List:\n"));
			for(int i=0;i<group_count;i++) {
				sprintf(buffer, "%d. %s\n", i+1, groups[i]->name);
				write(cli->sockfd, buffer, strlen(buffer));
			}

		} else if(strncmp(buff_out,ADD_CONTACT,strlen(ADD_CONTACT)) == 0) {
			int duplicate = 0;
			int i = 0;
			int found = 1;

			substring(buff_out, contact_name, strlen(ADD_CONTACT)+1, strlen(buff_out));
			trim_leading(contact_name);

			while(strcmp(cli->contacts[i],"\0") != 0) {
				if(strcmp(cli->contacts[i],contact_name) == 0) {
					sprintf(buffer, "Contact %s already exists.\n", contact_name);
					write(cli->sockfd, buffer, strlen(buffer));

					duplicate = 1;
					break;
				}
				i++;
			}

			if(duplicate == 0) {
				strcpy(cli->contacts[i],contact_name);

				file=fopen("users.txt","r");
				if(file==NULL) {
					perror("Error opening users.txt\n");
				}
				temp=fopen("temp.txt","a+");
				if(file==NULL) {
					perror("Error opening temp.txt\n");
				}

				while ((read = getline(&line, &len, file)) != -1) {

					if(found == 0) {
						if(strncmp(line,"contacts",8)==0) {

							str_trim_lf(line,strlen(line));
							char new[BUFFER_SZ] = "";
							sprintf(new, ":%s\n", contact_name);
							strcat(line,new);
							printf("New line with contacts is: %s\n", line);

							fputs(line, temp);
							found = 1;
							continue;
						}
					}

					fputs(line, temp);
			    char *ptr=strtok(line,":");
					int i = 0;
			    char *array[2];

			    while (ptr != NULL) {
			        array[i++] = ptr;
			        ptr = strtok (NULL, "\n");
			    }

					if(strcmp(array[0],cli->name)==0) {
						printf("Found user with name: %s\n", cli->name );
						found = 0;
					}
			  }

				remove("users.txt");
				rename("temp.txt", "users.txt");

				sprintf(buffer, "Contact %s was added to your list.\n", contact_name);
				write(cli->sockfd, buffer, strlen(buffer));

				fclose(file);
				fclose(temp);
			}

			bzero(buffer,BUFFER_SZ);

		} else if(strncmp(buff_out,DELETE_CONTACT,strlen(DELETE_CONTACT)) == 0) {
			char con_name[STR_SIZE];

			substring(buff_out, con_name, strlen(DELETE_CONTACT)+1, strlen(buff_out));
			trim_leading(con_name);

			int exists = contact_exists(con_name,cli);
			int pos=-1;

			if(exists == 0) {

				for(int i=0;i<MAX_CONTACTS;i++) {
					if(strcmp(cli->contacts[i],con_name)==0) {
						pos=i;
					}
				}

				for(int i=pos;i<MAX_CONTACTS;i++) {
						strcpy(cli->contacts[i],cli->contacts[i+1]);
				}

				bzero(buffer,BUFFER_SZ);
				sprintf(buffer,"%s:%s",cli->name,pswd);
				int user_start_line = search_in_file("users.txt",buffer,1);
				int line_index = 1;

				file = fopen("users.txt","r");
				temp=fopen("temp.txt","a+");
				if(file==NULL || temp==NULL) {
					perror("Error opening file.\n");
				}

				while ((read = getline(&line, &len, file)) != -1) {
					if(line_index != user_start_line+1) {
						fputs(line,temp);
					} else {
						char temp_buff[BUFFER_SZ];
						char new_line[BUFFER_SZ];
						strcpy(temp_buff,line);
						str_trim_lf(temp_buff,strlen(temp_buff));
						char *p=strtok(temp_buff,":");

						strcpy(new_line,"contacts:");

				    while (p != NULL) {
								p = strtok (NULL, ":");
								if(p != NULL && strcmp(p,"\0") != 0 && strcmp(p,"\n") != 0
									&& strncmp(p,con_name,strlen(con_name))!=0 ){
									sprintf(new_line + strlen(new_line),":%s",p);
								}
				    }

						fputs(new_line,temp);
						fputs("\n",temp);
					}
					line_index++;
				}

				remove("users.txt");
				rename("temp.txt", "users.txt");

				fclose(temp);
				fclose(file);

				write(cli->sockfd,"Contact deleted.\n",strlen("Contact deleted.\n"));

			} else {
				write(cli->sockfd,"Contact does not exist.\n",strlen("Contact does not exist.\n"));
			}

		} else if (strcmp(buff_out,CONTACT_LIST) == 0) {
			// show contact list
			int i=0;
			write(cli->sockfd, "Your Contact List:\n", strlen("Your Contact List:\n"));
			while(strcmp(cli->contacts[i],"\0") != 0) {
				sprintf(buffer, "%d. %s\n", i+1, cli->contacts[i]);
				write(cli->sockfd, buffer, strlen(buffer));
				i++;
			}
			bzero(buffer,BUFFER_SZ);

		} else if(strncmp(buff_out,PERSONAL_MESSAGE, strlen(PERSONAL_MESSAGE)) == 0) {

			char message[BUFFER_SZ];
			substring(buff_out, buff_out, 4, strlen(buff_out));

			char temp[BUFFER_SZ];
			strcpy(temp,buff_out);
			char *ptr = strtok(temp, " ");
			strcpy(contact_name,ptr);

			trim_leading(contact_name);

			substring(buff_out, message, strlen(contact_name)+2, strlen(buff_out));
			int res = send_pm(message, contact_name, cli);
			if (res == -1) {
				sprintf(buffer, "User %s is not in your contact list. Message not sent.\n", contact_name);
				write(cli->sockfd, buffer, strlen(buffer));
			} else if (res == 0) {
				sprintf(buffer, "%s is offline. Message not sent.\n", contact_name);
				write(cli->sockfd, buffer, strlen(buffer));
			}

		} else if(strncmp(buff_out,GROUP_MESSAGE,strlen(GROUP_MESSAGE)) == 0) {
			char message[BUFFER_SZ];
			char group_name[STR_SIZE];

			substring(buff_out, buff_out, strlen(GROUP_MESSAGE)+1, strlen(buff_out));

			char temp[BUFFER_SZ];
			strcpy(temp,buff_out);
			char *ptr = strtok(temp, " ");
			strcpy(group_name,ptr);

			trim_leading(group_name);

			substring(buff_out, message, strlen(group_name)+2, strlen(buff_out));
			printf("Message to group %s is: %s\n", group_name,message);

			int res = send_gm(message,group_name,cli);
			if(res == -1) {
				write(cli->sockfd, "Group does not exist.\n", strlen("Group does not exist.\n"));
			} else if(res == -2) {
				write(cli->sockfd, "You are not a member of the group.\n", strlen("You are not a member of the group.\n"));
			}

		} else if (receive > 0){
			if(strlen(buff_out) > 0){
				send_message(buff_out, cli->uid);

				str_trim_lf(buff_out, strlen(buff_out));
				printf("%s -> %s\n", buff_out, cli->name);
			}
		} else if (receive == 0 || strcmp(buff_out, "exit") == 0){
			sprintf(buff_out, "%s has left\n", cli->name);
			printf("%s", buff_out);
			send_message(buff_out, cli->uid);
			leave_flag = 1;
		} else {
			printf("ERROR: -1\n");
			leave_flag = 1;
		}

		bzero(buff_out, BUFFER_SZ);
	}

  /* Delete client from queue and yield thread */
	EXIT:
	close(cli->sockfd);
	memset(cli->contacts, '\0', sizeof cli->contacts);
  queue_remove(cli->uid);
  free(cli);
  cli_count--;
  pthread_detach(pthread_self());

	return NULL;
}

int main(int argc, char **argv){
	if(argc != 2){
		printf("Usage: %s <port>\n", argv[0]);
		return EXIT_FAILURE;
	}

	char *ip = "127.0.0.1";
	int port = atoi(argv[1]);
	int option = 1;
	int listenfd = 0, connfd = 0;
  struct sockaddr_in serv_addr;
  struct sockaddr_in cli_addr;
  pthread_t tid;

	FILE *groups_file;
	char *line = NULL;
  size_t len = 0;
  ssize_t read;

  /* Socket settings */
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr(ip);
  serv_addr.sin_port = htons(port);

  /* Ignore pipe signals */
	signal(SIGPIPE, SIG_IGN);

	if(setsockopt(listenfd, SOL_SOCKET,(SO_REUSEPORT | SO_REUSEADDR),(char*)&option,sizeof(option)) < 0){
		perror("ERROR: setsockopt failed.\n");
    return EXIT_FAILURE;
	}

	/* Bind */
  if(bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    perror("ERROR: Socket binding failed.\n");
    return EXIT_FAILURE;
  }

  /* Listen */
  if (listen(listenfd, 10) < 0) {
    perror("ERROR: Socket listening failed.\n");
    return EXIT_FAILURE;
	}

	/* Initialize groups */
	if((groups_file = fopen("groups.txt", "r")) == NULL) {
		perror("ERROR: Opening groups file failed.\n");
		return EXIT_FAILURE;
	}

	printf("Initializing groups...\n");

	while ((read = getline(&line, &len, groups_file)) != -1) {

		char temp[BUFFER_SZ];
		strcpy(temp,line);
		char *ptr=strtok(temp,":");
		char *array[2];

		int j=0;
		while (ptr != NULL) {
				array[j++] = ptr;
				ptr = strtok (NULL, "\n");
		}

		if((group_count + 1) == MAX_GROUPS) {
				printf("Max groups reached. Rejecting the rest...\n");
				break;
		}

		group_t *gr = (group_t *)malloc(sizeof(group_t));
		strcpy(gr->name,array[0]);
		str_trim_lf(array[1],strlen(array[1]));
		strcpy(gr->admin,array[1]);
		queue_add_group(gr);
		group_count++;
	}

	printf("Total groups %d\n", group_count);

	fclose(groups_file);

	printf("=== WELCOME TO THE CHATROOM ===\n");

	while(1){
		socklen_t clilen = sizeof(cli_addr);
		connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

		/* Check if max clients is reached */
		if((cli_count + 1) == MAX_CLIENTS){
			printf("Max clients reached. Rejected: .\n");
			print_client_addr(cli_addr);
			printf(":%d\n", cli_addr.sin_port);
			close(connfd);
			continue;
		}

		/* Client settings */
		client_t *cli = (client_t *)malloc(sizeof(client_t));
		cli->address = cli_addr;
		cli->sockfd = connfd;
		cli->uid = uid++;

		/* Add client to the queue and fork thread */
		queue_add(cli);
		pthread_create(&tid, NULL, &handle_client, (void*)cli);

		/* Reduce CPU usage */
		sleep(1);
	}

	return EXIT_SUCCESS;
}
