#include <iostream>
#include "network.h" // include this before rougueutil orelse windows breaks
#include "rogueutil.h"
#include "stopHandler.h"
#include <vector>
#include <cstdbool>
/**Network protocol
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
#endif
using namespace std;
using namespace rogueutil;

int width;
int height;
bool gameRunning=true;
bool paused=false;
bool gameWon = false;
bool ready = false;

typedef struct position{
	int x;
	int y;
}position;

typedef struct networkRecieveThreadInfo {
	SocketInterface * socket;
	vector<position> * apples;
} networkRecieveThreadInfo;

position useWindow;
vector<position> tmpOtherSnake;

void handleStop(){
	gameRunning=false;
	//handle stop here
	resetColor();
	cls();
	showcursor();

}

void render(char current[] , char prev[]);
void * inputThread(void *);
void * networkReadThread(void *);
#ifdef _WIN32
	DWORD WINAPI winThread(LPVOID params);
	DWORD WINAPI winThread2(LPVOID params);
#endif
void encodeInt(vector<uint8_t> &buffer, int data);
int decodeInt(vector<uint8_t> &buffer, int &pos);

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
	std::unique_ptr<SocketInterface> socketPtr;
	if (hosting) {
		cout << "Hosting new game"<<endl << "Enter server port: ";
		int port;
		cin >> port;
		cout << "Waiting for connection..." << endl;
		socketPtr = std::make_unique<SocketInterface>(port);
	}else {
		cout << "Joining game"<<endl<<"Enter server ip: ";
		string ip;
		int port;
		cin >> ip;
		cout << "Enter server port: ";
		cin >> port;
		socketPtr = std::make_unique<SocketInterface>(ip,port);
	}

	SocketInterface socket = *socketPtr;

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
	encodeInt(sendingBuffer, width);//encoder with
	encodeInt(sendingBuffer, height);//encode height
	socket.send(sendingBuffer); //send the with and height info

	vector<uint8_t> receivingBuffer = socket.receive(); // get the window size from the other player
	if (!socket.isConnected()) {
		cerr << "Disconnected" << endl;
		return EXIT_FAILURE;
	}
	sendingBuffer.clear();
	//at this time we only expect to recieve a window size paket so we will blindly try to decode it
	int revieveBufferPos = 0;
	position theirWindow = decodeWindowSize(receivingBuffer, revieveBufferPos);


	useWindow.x = min(theirWindow.x, width);
	useWindow.y = min(theirWindow.y, height);

	//now that we have an agreed upon window size, lets set up the player enviornment

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
	cls();//clear everything currenly on the screen
	//fill the real screen with white so that any un playable space will be knwon
	setBackgroundColor(WHITE);
	gotoxy(0,0);
	cout.flush();
	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			cout << "#";
		}
	}
	cout.flush();
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

	//wait for client to be ready
	encodeReady(sendingBuffer);//signal you are readdy
	socket.send(sendingBuffer);

	sendingBuffer.clear();


	//hidecursor();
	volatile CursorHider hideCursor;//automatically hides the cursor and unhides when deallocated

	networkRecieveThreadInfo netThreadInfo;
	netThreadInfo.socket = &socket;
	netThreadInfo.apples = &apples;

	//create the threads for input processing and network handling
	#ifdef _WIN32
		DWORD myThreadID;
		CreateThread(0, 0, winThread, &heading, 0, &myThreadID);
		DWORD myThreadID2;
		CreateThread(0, 0, winThread2, &netThreadInfo, 0, &myThreadID2);
	#else
		pthread_t inputThreadObject;
		pthread_create(&inputThreadObject, nullptr,inputThread,(void *)&heading);
		pthread_t inputThreadObject2;
		pthread_create(&inputThreadObject2, nullptr,networkReadThread,(void *)&netThreadInfo);
	#endif

	while (!ready && socket.isConnected()){}//wait for the client to be ready before starting the game

	//main game process loop
	while(gameRunning){
		if(paused){
			msleep(20);
			continue;
		}

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

		//Calculate the new snake
		position sp{};
		position tmp{};
		switch(heading){
			case 0:
				sp.x = snake[0].x;
				sp.y = snake[0].y-1;
				break;
			case 1:
				sp.x = snake[0].x+1;
				sp.y = snake[0].y;
				break;
			case 2:
				sp.x = snake[0].x;
				sp.y = snake[0].y+1;
				break;
			case 3:
				sp.x = snake[0].x-1;
				sp.y = snake[0].y;
				break;
		}//check to see if the snake has collided to its self or the other snake
		for(size_t i=0;i<snake.size();i++){
			if(snake[i].x == sp.x && snake[i].y == sp.y){
				gameRunning=false;
				encodeGameOver(sendingBuffer);
				break;
			}
		}
		for(size_t i=0;i<otherSnake.size();i++){
			if(otherSnake[i].x == sp.x && otherSnake[i].y == sp.y){
				gameRunning=false;
				encodeGameOver(sendingBuffer);
				break;
			}
		}

		//if the head is on an apple
		for(size_t i=0;i<apples.size();i++) {
			position apple = apples[i];
			if(snake[0].x == apple.x && snake[0].y == apple.y){
				apples.erase(apples.begin()+i);
				position np;
				snake.push_back(np);
				//apple regen will be handled on the host
				if (!hosting) {
					encodeAppleEat(sendingBuffer, apple);
				}
			}
		}

		if (hosting) {
			//only on the host
			while (apples.size()<4){
				position apple{};
				bool notValid = true;
				//generate a new valid apple position
				while(notValid){
					apple.x = rand()%(useWindow.x-5)+3;
					apple.y = rand()%(useWindow.y-5)+3;
					notValid=false;
					for(size_t i=0;i<snake.size();i++){
						if(snake[i].x == apple.x && snake[i].y == apple.y){
							notValid=true;
							break;
						}
					}
					for(size_t i=0;i<otherSnake.size();i++){
						if(otherSnake[i].x == apple.x && otherSnake[i].y == apple.y){
							notValid=true;
							break;
						}
					}
				}
				apples.push_back(apple);
			}
		}

		//handle moving this snake
		tmp = snake[0];
		snake[0] = sp;
		for(size_t i=1;i<snake.size();i++){
			position tmp2 = tmp;
			tmp = snake[i];
			snake[i]=tmp2;
		}

		for (size_t i = 0; i<apples.size();i++) {
			screen[apples[i].y*useWindow.x+apples[i].x] = 'A';
		}

		//add the new snake to the screen
		for(size_t i=0;i<snake.size();i++){
			screen[snake[i].y*useWindow.x+snake[i].x] = 'S';
		}
		for(size_t i=0;i<otherSnake.size();i++){
			screen[otherSnake[i].y*useWindow.x+otherSnake[i].x] = 'E';
		}
		if(sp.x<=0 || sp.x >=useWindow.x || sp.y <= 0 || sp.y >= useWindow.y){
			gameRunning = false;
			encodeGameOver(sendingBuffer);
		}

		//send snake info and updated apples if on the host
		encodeSnake(sendingBuffer, snake);
		if(hosting) {
			encodeApples(sendingBuffer, apples);
		}
		//other common things to send
		if (socket.isConnected()) {//if the socket is open then send the data
			socket.send(sendingBuffer);
		} else {
			//if not conncted then stop the game
			gameRunning = false;
		}

		msleep((heading % 2 ==0)?60:35);
		sendingBuffer.clear();
	}

	resetColor();
	showcursor();
	socket.close();
	gotoxy(2,useWindow.y -2);
	cout << "GAME OVER!! Score:" <<snake.size() <<" "<<(gameWon? "YOU WIN!!":"YOU LOOSE") << endl;


	showcursor();
	msleep(1000);//wait 1 second
	cout << "Press any key to continue"<<endl;
	anykey();
	//cursor should auto unhide but in case it does not this is here
	free(prevScreen);
	free(screen);
}

//render the current content to the screen
//S - your snake
//A - apple
//. - blank space
//E - other player
void render(char current[] , char prev[]){
	for(int x=0;x<useWindow.x;x++){
		for(int y=0;y<useWindow.y;y++){
			int index = x + y*useWindow.x;
			if(current[index] != prev[index]){
				prev[index] = current[index];
				switch(current[index]){
					case 'S'://your snake
						gotoxy(x,y);
						//set the color to green by default but make is yellow or brown when it gets close to the edge
						if(x==0 || x == useWindow.x-1 || y == 0 || y == useWindow.y-1){
							setBackgroundColor(BROWN);
						}else if(x <= 2 || x >= useWindow.x-3 || y <= 3 || y >= useWindow.y-3){
							setBackgroundColor(YELLOW);
						}else{
							setBackgroundColor(GREEN);
						}
						//re wright this char
						cout << "@";
						break;
					case 'A'://apple
						gotoxy(x,y);
						setBackgroundColor(RED);
						cout << " ";
						break;
					case 'E'://other player's snake (enemy)
						gotoxy(x,y);
						setBackgroundColor(BLUE);
						cout << " ";
						break;
					case '.'://any empty tile
						gotoxy(x,y);
						resetColor();
						cout << " ";

				}
			}
		}
	}
	cout.flush();
}

void * inputThread(void * args){
	int * headingDirection= (int*)args;
	while(gameRunning){
		int key = getkey();
		int facAx = *headingDirection%2;
		if((key == KEY_UP || key =='w' || key == 'W')&& facAx==1){
			*headingDirection=0;
		}
		if((key == KEY_RIGHT || key =='d' || key == 'D')  && facAx==0){
			*headingDirection=1;
		}
		if((key == KEY_DOWN || key =='s' || key == 'S') && facAx==1){
			*headingDirection=2;
		}
		if((key == KEY_LEFT || key =='a' || key == 'A')  && facAx==0){
			*headingDirection=3;
		}
		if(key == 'p' || key == 'P'){
			paused = !paused;
		}
		if(key == 'q' || key == 'Q'){
			gameRunning=false;
		}
		if(key == 'h' || key == 'H'){
			resetColor();
			gotoxy(1,1);
			cout << "Arrow Keys / WASD - change direction" << endl << "P - pause" << endl <<"Q - quit"<<endl<<"H - display this message";
		}
	}
	showcursor();//just in case
	return nullptr;
}

#ifdef _WIN32
	DWORD WINAPI winThread(LPVOID params){
		inputThread((void* )params);
		return 0;
	}
	DWORD WINAPI winThread2(LPVOID params){
		networkReadThread((void* )params);
		return 0;
	}
#endif

void encodeInt(vector<uint8_t> &buffer, int data) {
	//we are assuming the size of an int is 4 bytes

	//this is not at all sketchy, stop asking
	const uint8_t * db = reinterpret_cast<uint8_t *>(&data);
	buffer.push_back(db[0]);
	buffer.push_back(db[1]);
	buffer.push_back(db[2]);
	buffer.push_back(db[3]);
}
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
void encodeApples(vector<uint8_t> &buffer, vector<position> &apples) {
	buffer.push_back(APPLES_N);
	encodeInt(buffer, static_cast<int>(apples.size())*8+9);
	encodeInt(buffer, static_cast<int>(apples.size()));
	for (int i = 0; i < apples.size(); i++) {
		encodeInt(buffer,apples[i].x);
		encodeInt(buffer,apples[i].y);
	}
}
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
	for (int i = 0; i < numApples; i++) {
		position apple;
		apple.x = decodeInt(buffer, pos);
		apple.y = decodeInt(buffer, pos);
		output.push_back(apple);
	}
	return output;
}

void encodeSnake(vector<uint8_t> &buffer, vector<position> &snake) {
	buffer.push_back(SNAKE_N);
	encodeInt(buffer, static_cast<int>(snake.size())*8+9);
	encodeInt(buffer, static_cast<int>(snake.size()));
	for (int i = 0; i < snake.size(); i++) {
		encodeInt(buffer,snake[i].x);
		encodeInt(buffer,snake[i].y);
	}
}
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
	for (int i = 0; i < snakeLength; i++) {
		position snakeSeg;
		snakeSeg.x = decodeInt(buffer, pos);
		snakeSeg.y = decodeInt(buffer, pos);
		output.push_back(snakeSeg);
	}
	return output;
}

void encodeAppleEat(vector<uint8_t> &buffer, position &appleEat) {
	buffer.push_back(APPLE_EAT_N);
	encodeInt(buffer, 13);
	encodeInt(buffer, appleEat.x);
	encodeInt(buffer, appleEat.y);
}

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

void encodeGameOver(vector<uint8_t> &buffer) {
	buffer.push_back(GAME_OVER_N);
	encodeInt(buffer, 5);
}
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

void encodeReady(vector<uint8_t> &buffer) {
	buffer.push_back(READY_N);
	encodeInt(buffer, 5);
}
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

void * networkReadThread(void * thread_data) {
	const auto * info = static_cast<networkRecieveThreadInfo *>(thread_data);
	SocketInterface * socket = info -> socket;
	while (socket->isConnected()) {
		vector<uint8_t> receivedData = socket->receive();
		int dataPos = 0;
		//while a packet has not been fully possessed yet
		while (dataPos < receivedData.size()) {
			uint8_t packetType = receivedData[dataPos];

			if (packetType == SNAKE_N) {//this packet is the other players snake
				const vector<position> incomingSnake = decodeSnake(receivedData, dataPos);
				tmpOtherSnake = incomingSnake;
			} else if (packetType == APPLES_N){//this packet is the current apples
				vector<position> newApples = decodeApples(receivedData, dataPos);
				info ->apples->clear();
				//perhaps revisit to make more thread safe
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
			} else if (packetType == GAME_OVER_N) {
				gameRunning = false;
				gameWon = true;
				decodeGameOver(receivedData, dataPos);
			} else if (packetType == READY_N) {
				decodeReady(receivedData, dataPos);
				ready = true;
			} else {
				cerr << "unknown packet type " << packetType << endl;
				break;
			}

		}
	}
	return nullptr;
}