/*This project uses TCP sockets for client to client communication.
 *This was chosen because for most of the communications this game makes, it is important that they arrive in order and intact.
 *additionally it is important to know if the other client is still connected so the game can be ended if communications get interupted
 */
#include <iostream>
#include "network.h" // include this before rougueutil or else windows breaks
#include "rogueutil.h"
#include "stopHandler.h"
#include <vector>
#include <cstdbool>
/*Network protocol
 * TypeID - 1 byte, contentLength - 4 bytes, content  - length - 5 bytes
 *
 * types:
 * WINDOW_SIZE_N - id 1, length 13,data: client with - int, client height - int
 * APPLES_N - id 2, length 9 + 8*numapples, data: number apples -int, (repeats) apple X - int apple Y - int
 * SNAKE_N - id 3, length 9 + 8*snake length, data snake length - int, (repeats) snake X - int snake Y - int
 * APPLE_EAT_N - id 4, length 13, data: apple X - int, apple Y - int
 * GAME_OVER_N - id 5, length 5
 * READY_N - id 6, length 5
*/
#define WINDOW_SIZE_N 1
#define APPLES_N 2
#define SNAKE_N 3
#define APPLE_EAT_N 4
#define GAME_OVER_N 5
#define READY_N 6


#ifdef _WIN32
	#include <windows.h>
#else
	#include <pthread.h>
    #include <memory>
	#include <cstring>
#endif
using namespace std;
using namespace rogueutil;

//the with and height of your terminal
int width;
int height;
//if the game is currently running
bool gameRunning=true;
//if you have won the game
bool gameWon = false;
//if the other client is ready to start
bool ready = false;

typedef struct position{
	int x;
	int y;
}position;

typedef struct networkReceiveThreadInfo {
	SocketInterface * socket;
	vector<position> * apples;
} networkReceiveThreadInfo;

position useWindow;
vector<position> tmpOtherSnake;

//funciton to be run when a ctrl + C event is intercepted
void handleStop(){
	gameRunning=false;//tell the game to stop
	resetColor();//attempt to rest the current terminal color
	cls();//clear the screen
	showcursor();//attempt to show the cursor again

}

void render(char current[] , char prev[]);
void * inputThread(void *);
void * networkReadThread(void *);
#ifdef _WIN32
	//thead wrapper functions for windows
	DWORD WINAPI winThread(LPVOID params);
	DWORD WINAPI winThread2(LPVOID params);
#endif
//basic binary encoding / decoding functions
void encodeInt(vector<uint8_t> &buffer, int data);
int decodeInt(vector<uint8_t> &buffer, int &pos);

//complex structure encoding / decoding functions
position decodeWindowSize(vector<uint8_t> &buffer, int &pos);
void encodeApples(vector<uint8_t> &buffer, vector<position> &apples);
vector<position> decodeApples(vector<uint8_t> &buffer, int &pos);
void encodeSnake(vector<uint8_t> &buffer, vector<position> &snake);
vector<position> decodeSnake(vector<uint8_t> &buffer, int &pos);
void encodeAppleEat(vector<uint8_t> &buffer, position &appleEat);
position decodeAppleEat(vector<uint8_t> &buffer, int &pos);
void encodeGameOver(vector<uint8_t> &buffer);
bool decodeGameOver(vector<uint8_t> &buffer, int &pos);
void encodeReady(vector<uint8_t> &buffer);
bool decodeReady(vector<uint8_t> &buffer, int &pos);

int main() {
	//get the size of the terminal window
	width = tcols();
	height = trows();
	//terminal size check;
	if(width < 30 || height < 30){
		cout << "Terminal Window too Small!!" << endl;
		return EXIT_FAILURE;
	}
	saveDefaultColor();//save current terminal colors
	setConsoleTitle("SNAKE!!");

	//multy player init
	cout << "(H)ost or (J)oin? ";
	char sessionChoice;
	cin >> sessionChoice;
	//convert to uppercase if it was lower case
	if (sessionChoice > 'a') {
		sessionChoice -= ('a'-'A');
	}

	bool hosting = sessionChoice == 'H';
	std::unique_ptr<SocketInterface> socketPtr;//tmp smart pointer to hold uninitialized socket interface
	if (hosting) {
		//if hosting collect the port and create the socket interface
		cout << "Hosting new game" << endl << "Enter server port: ";
		int port;
		cin >> port;
		cout << "Waiting for connection..." << endl;
		socketPtr = std::make_unique<SocketInterface>(port);
	}else {
		//if joining collect the ip and port, then create the socket interface
		cout << "Joining game"<<endl<<"Enter server ip: ";
		string ip;
		int port;
		cin >> ip;
		cout << "Enter server port: ";
		cin >> port;
		socketPtr = std::make_unique<SocketInterface>(ip,port);
	}

	SocketInterface socket = *socketPtr;//get an instance variable of the socket interface

	//connect to the other client (if hosting start the server and wait for a connection)
	bool connectionSuccess = socket.connect();
	if (!connectionSuccess) {
		cerr << "Connection failed!" << endl;
		return EXIT_FAILURE;
	}
	cout << "Connection established!" << endl;

	//send your window size to the other player
	vector<uint8_t> sendingBuffer;
	//the first byte is the type
	sendingBuffer.push_back(WINDOW_SIZE_N);
	encodeInt(sendingBuffer, 13);//content length
	encodeInt(sendingBuffer, width);//encode with
	encodeInt(sendingBuffer, height);//encode height
	socket.send(sendingBuffer); //send the with and height info

	vector<uint8_t> receivingBuffer = socket.receive(); // get the window size from the other player
	if (!socket.isConnected()) {
		cerr << "Disconnected" << endl;
		return EXIT_FAILURE;
	}
	sendingBuffer.clear();//reset the sending buffer
	//at this time we only expect to receive a window size paket so we will blindly try to decode it
	int revieveBufferPos = 0;
	position theirWindow = decodeWindowSize(receivingBuffer, revieveBufferPos);

	//calculate the minimum common window size between the clients
	useWindow.x = min(theirWindow.x, width);
	useWindow.y = min(theirWindow.y, height);
	//use that size as the valid plyable area

	//now that we have an agreed upon window size, lets set up the player environment

	stopHandler::setContrlCHandler(&handleStop);//register the handler for ctrl c
	//Microsoft visual c++ compiler does not allow arrays to be defined with variables :'(
	char * prevScreen = (char *)malloc(useWindow.y*useWindow.x*sizeof(char));
	memset(prevScreen,0,useWindow.y*useWindow.x*sizeof(char));//initialize the previous screen to all 0s
	//y, x
	char * screen = (char *)malloc(useWindow.y*useWindow.x*sizeof(char));
	//initialize the screen with emptiness
	for(int i=0;i<useWindow.x;i++){
		for(int j=0;j<useWindow.y;j++){
			screen[j*useWindow.x+i] = '.';
		}
	}
	cls();//clear everything currently on the screen
	//fill the real screen with white so that any un playable space will be known
	setBackgroundColor(WHITE);
	gotoxy(0,0);//might not be necessary
	cout.flush();//ensure the screen is colored, positioned and ready to be filled
	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			cout << "#";//fill the screen with white #
		}
	}
	cout.flush();//force the changes to go through
	resetColor();
	cout.flush();

	int heading =0;
	vector<position> apples;

	//if on the host spawn the first 4 apples
	if (hosting) {
		apples.push_back({5,5});
		while (apples.size() < 4) {// ensure there are 4 apples
			position apple{};
			apple.x = rand()%(useWindow.x-5)+3;
			apple.y = rand()%(useWindow.y-5)+3;
			apples.push_back(apple);
		}
		//apply the apples to the screen
		for (int i = 0; i < apples.size(); i++) {
			screen[apples[i].y*useWindow.x+apples[i].x] = 'A';
		}

		//send the apple info to the client
		encodeApples(sendingBuffer, apples);
		socket.send(sendingBuffer);
		sendingBuffer.clear();
	}
	vector<position> snake;
	vector<position> otherSnake;
	//set the initial positions of the snakes and add them to the screen
	if (hosting) {
		position p;
		p.x=useWindow.x/4;
		p.y=useWindow.y/2;
		snake.push_back(p);
		position p2;
		p2.x=useWindow.x/4+useWindow.x/2;
		p2.y=useWindow.y/2;
		otherSnake.push_back(p2);
		tmpOtherSnake.push_back(p2);

		screen[p.y*useWindow.x+p.x] = 'S';
		screen[p2.y*useWindow.x+p2.x] = 'E';
	} else {
		position p;
		p.x=useWindow.x/4+useWindow.x/2;
		p.y=useWindow.y/2;
		snake.push_back(p);
		position p2;
		p2.x=useWindow.x/4;
		p2.y=useWindow.y/2;
		otherSnake.push_back(p2);
		tmpOtherSnake.push_back(p2);

		screen[p.y*useWindow.x+p.x] = 'S';
		screen[p2.y*useWindow.x+p2.x] = 'E';
	}

	//render the initial screen
	render(screen,prevScreen);

	encodeReady(sendingBuffer);//signal you are ready to the other client
	socket.send(sendingBuffer);
	sendingBuffer.clear();


	//hidecursor();
	volatile CursorHider hideCursor;//automatically hides the cursor and unhides when deallocated

	networkReceiveThreadInfo netThreadInfo;
	netThreadInfo.socket = &socket;
	netThreadInfo.apples = &apples;

	//create the threads for input processing and network handling
	#ifdef _WIN32
		//create the threads on windows
		DWORD myThreadID;
		CreateThread(0, 0, winThread, &heading, 0, &myThreadID);
		DWORD myThreadID2;
		CreateThread(0, 0, winThread2, &netThreadInfo, 0, &myThreadID2);
	#else
		//create the threads on not windows
		pthread_t inputThreadObject;
		pthread_create(&inputThreadObject, nullptr,inputThread,(void *)&heading);
		pthread_t inputThreadObject2;
		pthread_create(&inputThreadObject2, nullptr,networkReadThread,(void *)&netThreadInfo);
	#endif

	while (!ready && socket.isConnected()){}//wait for the client to be ready before starting the game
	//the ready signal comes through on the network receive thread

	//main game process loop
	while(gameRunning){//while the game is running

		//display any parts of the screen that have been updated
		render(screen,prevScreen);

		//Remove the old snake from the screen
		for(size_t i=0;i<snake.size();i++){
			screen[snake[i].y*useWindow.x+snake[i].x] = '.';
		}
		//remove the old enemy snake from the screen
		for(size_t i=0;i<otherSnake.size();i++){
			screen[otherSnake[i].y*useWindow.x+otherSnake[i].x] = '.';
		}
		//swap the other snake for the most recent one from the network
		otherSnake = tmpOtherSnake;

		//Calculate your new snake
		position snakeHeadPos{};
		switch(heading){
			case 0://moving up
				snakeHeadPos.x = snake[0].x;
				snakeHeadPos.y = snake[0].y-1;
				break;
			case 1://moving right
				snakeHeadPos.x = snake[0].x+1;
				snakeHeadPos.y = snake[0].y;
				break;
			case 2://moving down
				snakeHeadPos.x = snake[0].x;
				snakeHeadPos.y = snake[0].y+1;
				break;
			case 3://moving left
				snakeHeadPos.x = snake[0].x-1;
				snakeHeadPos.y = snake[0].y;
				break;
		}
		//check to see if the snake has collided with its self or the other snake
		for(size_t i=0;i<snake.size();i++){
			if(snake[i].x == snakeHeadPos.x && snake[i].y == snakeHeadPos.y){
				//you hit your self
				gameRunning=false;
				encodeGameOver(sendingBuffer);//tell the other player they won
				break;
			}
		}
		for(size_t i=0;i<otherSnake.size();i++){
			if(otherSnake[i].x == snakeHeadPos.x && otherSnake[i].y == snakeHeadPos.y){
				//you hit them
				gameRunning=false;
				encodeGameOver(sendingBuffer);//tell the other player they won
				break;
			}
		}

		//check for eating an apple
		for(size_t i=0;i<apples.size();i++) {
			position apple = apples[i];
			if(snake[0].x == apple.x && snake[0].y == apple.y){ // if your head is on the apple
				apples.erase(apples.begin()+i);//remove that apple from the supply
				position np{};
				snake.push_back(np);// add a new segment to yourself
				//apple regen will be handled on the host
				if (!hosting) {
					encodeAppleEat(sendingBuffer, apple);//tell the host you ate an apple
				}
				break;
			}
		}

		if (hosting) {
			//only on the host
			while (apples.size()<4){//while more apples are needed
				position apple{};
				bool notValid = true;
				//generate a new valid apple position
				while(notValid){//perhaps I should set a max attempts value
					apple.x = rand()%(useWindow.x-5)+3;
					apple.y = rand()%(useWindow.y-5)+3;
					notValid=false;
					for(size_t i=0;i<snake.size();i++){//check it is not on the host's snake
						if(snake[i].x == apple.x && snake[i].y == apple.y){
							notValid=true;
							break;
						}
					}
					for(size_t i=0;i<otherSnake.size();i++){//check it is not on the other player's snake
						if(otherSnake[i].x == apple.x && otherSnake[i].y == apple.y){
							notValid=true;
							break;
						}
					}
				}
				apples.push_back(apple);//add the new apple to the apples
			}
		}

		//handle moving your snake
		position tmp = snake[0];
		snake[0] = snakeHeadPos;//set the new head to what was calculated
		for(size_t i=1;i<snake.size();i++){//move the whole body back by 1, discarding the last segment
			position tmp2 = tmp;
			tmp = snake[i];
			snake[i]=tmp2;
		}

		//wright all apples to the screen
		for (size_t i = 0; i<apples.size();i++) {
			screen[apples[i].y*useWindow.x+apples[i].x] = 'A';
		}

		//add the new snake to the screen
		for(size_t i=0;i<snake.size();i++){
			screen[snake[i].y*useWindow.x+snake[i].x] = 'S';
		}
		//add the other player to the screen
		for(size_t i=0;i<otherSnake.size();i++){
			screen[otherSnake[i].y*useWindow.x+otherSnake[i].x] = 'E';
		}
		//check if you have goan out of bounds
		if(snakeHeadPos.x<=0 || snakeHeadPos.x >=useWindow.x || snakeHeadPos.y <= 0 || snakeHeadPos.y >= useWindow.y){
			//if so end the game
			gameRunning = false;
			encodeGameOver(sendingBuffer);//tell the other player they won
		}

		//send snake info and updated apples if on the host
		encodeSnake(sendingBuffer, snake);
		if(hosting) {
			encodeApples(sendingBuffer, apples);
		}

		if (socket.isConnected()) {//if the socket is open then send the data
			socket.send(sendingBuffer);
		} else {
			//if not connected then stop the game
			gameRunning = false;
		}

		msleep((heading % 2 ==0)?60:35);//wait for a moment before the next frame, if you were traveling up or down wait a little longer to make the snake feel like it is moving at a similar speed
		sendingBuffer.clear();//clear the current send buffer
	}//end of while game running

	resetColor();//reset the printing color
	showcursor();//attempt to manually show the cursor
	socket.close();//attempt to close the socket
	gotoxy(2,useWindow.y -2);//move the cursor to near the bottom of the screen
	cout << "GAME OVER!! Score:" <<snake.size() <<" "<<(gameWon? "YOU WIN!!":"YOU LOOSE") << endl;//print the game over message


	showcursor();//again try to show the cursor
	cout.flush();
	msleep(1000);//wait 1 second
	cout << "Press any key to continue"<<endl;
	anykey();//wait for a key to be pressed

	//cursor should auto unhide but in case it does not this is here
	free(prevScreen);//free the memory for the screen and previous screen
	free(screen);
}

//render the current content to the screen
//S - your snake
//A - apple
//. - blank space
//E - other player
void render(char current[] , char prev[]){
	//loop through the screen
	for(int x=0;x<useWindow.x;x++){
		for(int y=0;y<useWindow.y;y++){
			int index = x + y*useWindow.x;
			//check if the value at the current postion has chnaged
			if(current[index] != prev[index]){
				//if so then figure out how to update it
				prev[index] = current[index];
				gotoxy(x,y);//positon the cursor at the screen lcoation
				switch(current[index]){
					case 'S'://your snake
						//set the color to green by default but make is yellow or brown when it gets close to the edge
						if(x==0 || x == useWindow.x-1 || y == 0 || y == useWindow.y-1){
							setBackgroundColor(BROWN);
						}else if(x <= 2 || x >= useWindow.x-3 || y <= 3 || y >= useWindow.y-3){
							setBackgroundColor(YELLOW);
						}else{
							setBackgroundColor(GREEN);
						}
						cout << "@";
						break;
					case 'A'://apple
						//TODO make the other things have unique chars incase the colors are not working correct on your terminal
						setBackgroundColor(RED);
						cout << " ";
						break;
					case 'E'://other player's snake (enemy)
						setBackgroundColor(BLUE);
						cout << " ";
						break;
					case '.'://any empty tile
						resetColor();
						cout << " ";

				}
			}
		}
	}
	cout.flush();//enure the screen updates are applied to the screen (looking at you Linux/Mac)
}

//thread for processing player inputs
void * inputThread(void * args){
	int * headingDirection = (int*)args;
	while(gameRunning){//while the game is running
		int key = getkey();//read what was pressed
		int facingAxis = *headingDirection%2; //get what axis the player is facing in
		if((key == KEY_UP || key =='w' || key == 'W')&& facingAxis==1){//if w / up was pressed
			*headingDirection=0;
		}
		if((key == KEY_RIGHT || key =='d' || key == 'D')  && facingAxis==0){//if d / right was pressed
			*headingDirection=1;
		}
		if((key == KEY_DOWN || key =='s' || key == 'S') && facingAxis==1){//if s / down was pressed
			*headingDirection=2;
		}
		if((key == KEY_LEFT || key =='a' || key == 'A')  && facingAxis==0){//if a / left was pressed
			*headingDirection=3;
		}
		if(key == 'q' || key == 'Q'){//if q was pressed then quit the game
			gameRunning=false;
			//TODO tell the other client that they won
		}
		if(key == 'h' || key == 'H'){//if h was pressed
			//print the controls help message in the top corner of the screen
			resetColor();
			gotoxy(1,1);
			cout << "Objective: survive longer then and grow longer then your opponent" << endl << "Controls:" << endl << "Arrow Keys / WASD - change direction" << endl <<"Q - quit"<<endl<<"H - display this message";
		}
	}
	showcursor();//just in case
	return nullptr;
}

#ifdef _WIN32
	//windows thread wrapper functions
	DWORD WINAPI winThread(LPVOID params){
		inputThread((void* )params);
		return 0;
	}
	DWORD WINAPI winThread2(LPVOID params){
		networkReadThread((void* )params);
		return 0;
	}
#endif

//network data encoding / decoding functions

//wright an int to the buffer
void encodeInt(vector<uint8_t> &buffer, int data) {
	//we are assuming the size of an int is 4 bytes

	//this is not at all sketchy, stop asking
	const uint8_t * db = reinterpret_cast<uint8_t *>(&data);
	buffer.push_back(db[0]);
	buffer.push_back(db[1]);
	buffer.push_back(db[2]);
	buffer.push_back(db[3]);
}
//read an int from the buffer and advance pos
int decodeInt(vector<uint8_t> &buffer, int &pos) {
	//again not at all sketchy
	int output = 0;
	auto * db = reinterpret_cast<uint8_t *>(&output);
	db[0] = buffer[pos];
	pos++;
	db[1] = buffer[pos];
	pos++;
	db[2] = buffer[pos];
	pos++;
	db[3] = buffer[pos];
	pos++;
	return output;
}

//read a window size packet from the buffer and advance pos
position decodeWindowSize(vector<uint8_t> &buffer, int &pos) {
	//validate things are prbly transmitted correctly
	if (buffer[pos] != WINDOW_SIZE_N) {
		cerr << "attempt to decode window size but data was not window size! "<<endl;
		return {-1,-1};
	}
	int totalContentLength = static_cast<int>(buffer.size()) - pos;
	pos++;
	int dataLength = decodeInt(buffer, pos);
	if (dataLength != 13) {
		cerr << "window size packet did not have the correct length" << endl;
		return {-1,-1};
	}
	if (totalContentLength < dataLength) {
		cerr << "data transition failure. Window size incoming data not fully transmitted" << endl;
		return {-1,-1};
	}
	position output;
	//decode the actual data
	output.x = decodeInt(buffer, pos);
	output.y = decodeInt(buffer, pos);
	return output;
}

//APPLES_N - id 2, length 9 + 8*numapples, data: number apples -int, (repeats) apple X - int apple Y - int
//wright the apple positions to the buffer
void encodeApples(vector<uint8_t> &buffer, vector<position> &apples) {
	buffer.push_back(APPLES_N);
	encodeInt(buffer, static_cast<int>(apples.size())*8+9);
	encodeInt(buffer, static_cast<int>(apples.size()));
	for (int i = 0; i < apples.size(); i++) {
		encodeInt(buffer,apples[i].x);
		encodeInt(buffer,apples[i].y);
	}
}
//read the apple positions from the buffer and advance pos
vector<position> decodeApples(vector<uint8_t> &buffer, int &pos) {
	//validate things are prbly transmitted correctly
	if (buffer[pos] != APPLES_N) {
		cerr << "attempt to decode apples but data type was not apples! "<<endl;
		return {};
	}
	int totalContentLength = static_cast<int>(buffer.size()) - pos;
	pos++;
	int dataLength = decodeInt(buffer, pos);
	if (totalContentLength < dataLength) {
		cerr << "data transition failure. Apples incoming data not fully transmitted" << endl;
		return {};
	}
	int numApples = decodeInt(buffer, pos);
	vector<position> output(numApples);
	//read the data for each apple
	for (int i = 0; i < numApples; i++) {
		position apple;
		apple.x = decodeInt(buffer, pos);
		apple.y = decodeInt(buffer, pos);
		output.push_back(apple);
	}
	return output;
}
//wright your snake to the buffer
void encodeSnake(vector<uint8_t> &buffer, vector<position> &snake) {
	buffer.push_back(SNAKE_N);
	encodeInt(buffer, static_cast<int>(snake.size())*8+9);
	encodeInt(buffer, static_cast<int>(snake.size()));
	for (int i = 0; i < snake.size(); i++) {
		encodeInt(buffer,snake[i].x);
		encodeInt(buffer,snake[i].y);
	}
}
//read you opponents snake from the budder and advance pos
vector<position> decodeSnake(vector<uint8_t> &buffer, int &pos) {
	//validate things are prbly transmitted correctly
	if (buffer[pos] != SNAKE_N) {
		cerr << "attempt to decode snake but data type was not sbake! "<<endl;
		return {};
	}
	int totalContentLength = static_cast<int>(buffer.size()) - pos;
	pos++;
	int dataLength = decodeInt(buffer, pos);
	if (totalContentLength < dataLength) {
		cerr << "data transition failure. Snake incoming data not fully transmitted" << endl;
		return {};
	}
	int snakeLength = decodeInt(buffer, pos);
	vector<position> output(snakeLength);
	//read each segment of the snake from the buffer
	for (int i = 0; i < snakeLength; i++) {
		position snakeSeg;
		snakeSeg.x = decodeInt(buffer, pos);
		snakeSeg.y = decodeInt(buffer, pos);
		output.push_back(snakeSeg);
	}
	return output;
}
//wright the position of an eaten apple to the buffer
void encodeAppleEat(vector<uint8_t> &buffer, position &appleEat) {
	buffer.push_back(APPLE_EAT_N);
	encodeInt(buffer, 13);
	encodeInt(buffer, appleEat.x);
	encodeInt(buffer, appleEat.y);
}
//read the position of an eaten apple from the buffer and advance pos
position decodeAppleEat(vector<uint8_t> &buffer, int &pos) {
	if (buffer[pos] != APPLE_EAT_N) {
		cerr << "attempt to decode apple eat but data was not apple eat! "<<endl;
		return {-1,-1};
	}
	int totalContentLength = static_cast<int>(buffer.size()) - pos;
	pos++;
	int dataLength = decodeInt(buffer, pos);
	if (dataLength != 13) {
		cerr << "apple eat packet did not have the correct length" << endl;
		return {-1,-1};
	}
	if (totalContentLength < dataLength) {
		cerr << "data transition failure. apple eat incoming data not fully transmitted" << endl;
		return {-1,-1};
	}
	position output;
	//decode the actual data
	output.x = decodeInt(buffer, pos);
	output.y = decodeInt(buffer, pos);
	return output;
}
//wright a game over packet to the buffer
void encodeGameOver(vector<uint8_t> &buffer) {
	buffer.push_back(GAME_OVER_N);
	encodeInt(buffer, 5);
}
//read/validate a game over packet from the buffer and advance pos
bool decodeGameOver(vector<uint8_t> &buffer, int &pos) {
	if (buffer[pos] != GAME_OVER_N) {
		cerr << "attempt to decode game over but data was not game over! "<<endl;
		return false;
	}
	int totalContentLength = static_cast<int>(buffer.size()) - pos;
	pos++;
	int dataLength = decodeInt(buffer, pos);
	if (dataLength != 5) {
		cerr << "game over packet did not have the correct length" << endl;
		return false;
	}
	if (totalContentLength < dataLength) {
		cerr << "data transition failure. game over incoming data not fully transmitted" << endl;
		return false;
	}
	return true;
}
//wright a ready pacet to the buffer
void encodeReady(vector<uint8_t> &buffer) {
	buffer.push_back(READY_N);
	encodeInt(buffer, 5);
}
//read / validate a ready packet from the buffer and advance pos
bool decodeReady(vector<uint8_t> &buffer, int &pos) {
	if (buffer[pos] != READY_N) {
		cerr << "attempt to decode ready but data was not ready! "<<(int)buffer[pos]<<endl;
		return false;
	}
	int totalContentLength = static_cast<int>(buffer.size()) - pos;
	pos++;
	int dataLength = decodeInt(buffer, pos);
	if (dataLength != 5) {
		cerr << "ready packet did not have the correct length" << endl;
		return false;
	}
	if (totalContentLength < dataLength) {
		cerr << "data transition failure. ready data not fully transmitted" << endl;
		return false;
	}
	return true;
}

//thread for handling incoming network data while playing
void * networkReadThread(void * thread_data) {
	const auto * info = static_cast<networkReceiveThreadInfo *>(thread_data);
	SocketInterface * socket = info -> socket;
	while (socket->isConnected()) {//while the connection is open
		vector<uint8_t> receivedData = socket->receive();//read data from the socket
		int dataPos = 0;
		//while a packet has not been fully possessed yet
		while (dataPos < receivedData.size()) {//while we have not decoded all the data sent yet
			uint8_t packetType = receivedData[dataPos]; //get the packet type byte

			//check that type and try to decode it
			if (packetType == SNAKE_N) {//this packet is the other players snake
				const vector<position> incomingSnake = decodeSnake(receivedData, dataPos);
				tmpOtherSnake = incomingSnake;
			} else if (packetType == APPLES_N){//this packet is the current apples
				vector<position> newApples = decodeApples(receivedData, dataPos);
				info ->apples->clear();
				//perhaps revisit to make more thread safe			naaaa its fine :tm:
				for (int i = 0; i < newApples.size(); i++) {
					info ->apples->push_back(newApples[i]);
				}
			} else if (packetType == APPLE_EAT_N) {//this packet is an apple was eaten
				position appleEaten = decodeAppleEat(receivedData, dataPos);
				for (int i = 0; i < info ->apples->size(); i++) {
					if (info ->apples->at(i).x == appleEaten.x && info ->apples->at(i).y == appleEaten.y) {
						info ->apples->erase(info ->apples->begin() + i);
						break;
					}
				}
			} else if (packetType == GAME_OVER_N) {//if this packet is a game over packet
				gameRunning = false;
				gameWon = true;
				decodeGameOver(receivedData, dataPos);//validate  and advance the data position pointer
			} else if (packetType == READY_N) {//if this packet is a ready to start packet
				decodeReady(receivedData, dataPos);//validate and advance the data positon pointer
				ready = true;//we are ready to start
			} else {
				cerr << "unknown packet type " << packetType << endl;
				break;
			}

		}
	}
	return nullptr;
}