#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>

# define FATAL_ERROR "Fatal error\n"
# define BUFFER_SIZE 60000

# define CLIENT_JOINED 1
# define CLIENT_MESSAGES 2
# define CLIENT_LEAVED 3

# define bitset(byte,nbit)   (byte |= (1 << nbit))
# define bitclear(byte,nbit) (byte &= ~(1 << nbit))
# define bitcheck(byte,nbit) (byte & (1 << nbit))

typedef struct t_client client;

typedef struct t_client
{
	unsigned int opt;
	short int id;
	int	socket;
	int offset;
	char *message;
	char *to_send;
}	client;


typedef struct t_server
{
	int socket;
	int max_client;
	unsigned int nb_clients;
	char buffer[BUFFER_SIZE];
	client **clients;
	client *last_client;
}	server;


int extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

void print_and_exit(server *server, const char *message, int fd, int status)
{
	if (message) write(fd, message, strlen(message));

	if (server)
	{
		for (int i = 0; i <= FD_SETSIZE; i++)
		{
			close(server -> clients[i] -> socket);
			free(server -> clients[i] -> message);
			free(server -> clients[i] -> to_send);
		}
	}

	exit(status);	
}

void setup_server(server *server, const int port)
{
	server -> max_client = FD_SETSIZE + 1;

	struct sockaddr_in servaddr; 

	// socket create and verification 
	if ((server -> socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		print_and_exit(NULL, FATAL_ERROR, STDERR_FILENO, EXIT_FAILURE);

	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(port); 
	int option = 1;

	if (setsockopt(server -> socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option, sizeof(option)) < 0)
		print_and_exit(NULL, FATAL_ERROR, STDERR_FILENO, EXIT_FAILURE);

	// Binding newly created socket to given IP and verification 
	if ((bind(server -> socket, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
		print_and_exit(NULL, FATAL_ERROR, STDERR_FILENO, EXIT_FAILURE);

	if (listen(server -> socket, SOMAXCONN) != 0)
		print_and_exit(NULL, FATAL_ERROR, STDERR_FILENO, EXIT_FAILURE);

	if ((server -> clients = calloc(server -> max_client, sizeof(client *))) == NULL)
		print_and_exit(server, FATAL_ERROR, STDERR_FILENO, EXIT_FAILURE);

}

void broadcast_message(server *server, client **clients, client *client, int max_size)
{
	char c;

	if (bitcheck(client -> opt, CLIENT_JOINED))
		sprintf(server -> buffer, "server: client %d just arrived\n", client -> id);
	else if (bitcheck(client -> opt, CLIENT_MESSAGES))
	{
		c = client -> message[client -> offset + 1];
		client -> message[client -> offset + 1] = 0;
		sprintf(server -> buffer, "Client %d: ", client -> id);
	}
	else if (bitcheck(client -> opt, CLIENT_LEAVED))
		sprintf(server -> buffer, "server: client %d just left\n", client -> id);
	
	size_t message_len = bitcheck(client -> opt, CLIENT_MESSAGES) ? strlen(client -> message) : strlen(server -> buffer);

	for (int i = 0; i < max_size; i++)
	{
		if (clients[i] == client || clients[i] == NULL) continue;

		if (bitcheck(client -> opt, CLIENT_MESSAGES))
		{
			send(clients[i] -> socket, server -> buffer, strlen(server -> buffer), MSG_NOSIGNAL);		
			send(clients[i] -> socket, client -> message, message_len, MSG_NOSIGNAL);
		}
		else
			send(clients[i] -> socket, server -> buffer, message_len, MSG_NOSIGNAL);
	}
	
	if (bitcheck(client -> opt, CLIENT_MESSAGES))
	{
		client -> message[client -> offset + 1] = c;
		client -> to_send = malloc(sizeof(char) * strlen(client -> message) - client -> offset + 1);

		if (client -> to_send)
		{
			size_t j = 0;

			for (size_t i = client -> offset + 1; i < strlen(client -> message); i++, j++)
				client -> to_send[j] = client -> message[i];
			client -> to_send[j] = 0;
		}
	}
	
	if (client -> message)
	{
		free(client -> message);
		client -> message = NULL;
	}

	client -> opt = 0;
}

int find_rchr(const char *str, char c)
{
	int len = strlen(str);

	for (int i = len; i > -1 ; --i)
	{
		if (str[i] == c) return i;
	}

	return -1;	
}

void server_loop(server *server)
{
	fd_set readfds;
	int maxfd = server -> socket;
	client **clients = server -> clients; 

	while (1)
	{
		FD_ZERO(&readfds);

		FD_SET(server -> socket, &readfds);
		
		for (int i = 0; i < server -> max_client; i++)
		{
			if (clients[i])
			{
				FD_SET(clients[i] -> socket, &readfds);
				if (clients[i] -> socket > maxfd)
					maxfd = clients[i] -> socket;
			}
		}
		

		int events = select(maxfd + 1, &readfds, NULL, NULL, NULL);

		if (events < 0) print_and_exit(server, FATAL_ERROR, STDERR_FILENO, EXIT_FAILURE);

		if (FD_ISSET(server -> socket, &readfds))
		{
			int socket = accept(server -> socket, NULL, NULL);

			if (socket >= 0 && socket <= FD_SETSIZE)
			{
				if ((clients[socket] = calloc(1, sizeof(client))) != NULL)
				{
					clients[socket] -> id = server -> nb_clients++;
					clients[socket] -> socket = socket;
					bitset(clients[socket] -> opt, CLIENT_JOINED);
					broadcast_message(server, clients, clients[socket], server -> max_client);
				}
			}
		}

		for (int i = 0; i <= maxfd; i++)
		{
			if (FD_ISSET(i, &readfds) && i != server -> socket)
			{
				int bytes_read = recv(i, server -> buffer, BUFFER_SIZE, 0);
				server -> buffer[bytes_read] = 0;
				
				if (bytes_read <= 0)
				{
					bitset(clients[i] -> opt, CLIENT_LEAVED);
					broadcast_message(server, clients, clients[i], server -> max_client);
					free(clients[i] -> message);
					free(clients[i]);
					clients[i] = 0;
					continue;
				}

				size_t to_send_len = clients[i] -> to_send ?  strlen(clients[i] -> to_send) : 0;
				clients[i] -> message = calloc(sizeof(char), bytes_read + to_send_len + 1);

 				if (clients[i] -> to_send) strcat(clients[i] -> message, clients[i] -> to_send);
				strcat(&clients[i] -> message[to_send_len], server -> buffer);
				free(clients[i] -> to_send);
				clients[i] -> to_send = NULL;
				if (clients[i] -> message == NULL) continue;
				clients[i] -> offset = find_rchr(clients[i] -> message, '\n');
				if (clients[i] -> offset == -1) continue;
				bitset(clients[i] -> opt, CLIENT_MESSAGES);
				broadcast_message(server, clients, clients[i], server -> max_client);
			}
		}
	}	
}

int main(int argc, char **argv) 
{

	if (argc != 2) print_and_exit(NULL, "Wrong number of arguments\n", STDERR_FILENO, EXIT_FAILURE);
	char buffer[BUFFER_SIZE];
	write(STDOUT_FILENO, buffer, strlen(buffer));
	server server;
	memset(&server, 0, sizeof(server));
	setup_server(&server, atoi(argv[1]));
	server_loop(&server);
}