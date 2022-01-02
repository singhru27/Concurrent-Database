## Project Name & Description

This is an in-memory database written in C and an implementation of a client and server. The client can connect to the server using TCP socket based connections, and make changes to an underlying database. The database stores items as <key> <value> pairs in a binary search tree. All items are lexicographically ordered (i.e an in-order traversal of the tree yields a lexicographical ordering of the corresponding keys)

The server supports multithreaded database modifications using fine-grained locking (which is implemented using mutexes), signal handling, and can handle multiple clients at once. The server contains multiple functions that allow for the suspension of threads in execution, the restart of threads in execution, and for the printing of the database. 

The client supports adding, retrieving, and deleting keys from the server. The clients connects to the server using a socket-based TCP connection. The client can also execute sequences of commands from a file. Each client is serviced by a separate thread in the server



## Project Status

This project is completed

## Project Screen Shot(s)

#### Example:   

![ScreenShot](https://github.com/singhru27/Concurrent-Database/blob/master/screenshots/Home.png?raw=true)


## Installation and Setup Instructions

To build the program, run the command

```
make all
```

which compiles the database programs. To launch the server, run the command

```
/server <port number>
```

The database supports several commands. These commands are as follows:

```
"s" - Stops all threads
"g" - Restarts all currently stopped threads
"p" - Prints out the database in a pre-order fashion.
EOF - When EOF is received from stdin, all client connections are immediately terminated and the server exits cleanly
SIGINT - When the database receives a SIGINT, all client connections are immediately terminated via cancellation
```

Once the server is running, clients can be launched to connect to the server via a TCP connection. To launch a client, open a new terminal window and execute the following command
```
./client <hostname> <portnumber>
```

Once the client has successfully connected to the server, you can execute several different commands to carry out database modifications. These commands include the following
```
a <key> <value>: Adds <key> into the database with value <value>, if it is not already in the database.
q <key>: Retrieves the value stored with key <key>.
d <key>: Deletes the given key and its associated value from the database.
f <file>: Executes the sequence of commands contained in the specified file.
```

Scripts can be used to execute multiple database modifications with multiple concurrent client instances via the following command:
```
./client <hostname> <port> [script] [occurrences] 

```

To clean your directory once you are finished running the program, you can run the following from the shell:

```
make clean
```

