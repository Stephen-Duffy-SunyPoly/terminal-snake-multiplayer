#include <iostream>
#include "network.h" // include this before rougueutil orelse windows breaks
#include "rogueutil.h"
#include "stopHandler.h"
#include <vector>
#include <cstdbool>
/**Network protocol
 * TypeID - 1 bytes, contentLength - 4 bytes, content  - length bytes
 *
 * types:
 * WINDOW_SIZE_N - id 1, length 13,data: client with, client height
*/
#define WINDOW_SIZE_N 1


#ifdef _WIN32
	#include <windows.h>
#else
	#include <pthread.h>
#endif
using namespace std;
using namespace rogueutil;

int width;
int height;
bool gameRunning=true,paused=false;

typedef struct position{
	int x;
	int y;
}position;

void handleStop(){
	gameRunning=false;
	//handle stop here
	resetColor();
	cls();
	showcursor();

}

void render(char current[] , char prev[]);
void * inputThread(void *);
#ifdef _WIN32
	DWORD WINAPI winThread(LPVOID params);
#endif
void encodeInt(vector<uint8_t> &buffer, int data);
int decodeInt(vector<uint8_t> &buffer, int &pos);
position decodeWindowSize(vector<uint8_t> &buffer, int &pos);

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

	//at this time we only expect to recieve a window size paket so we will blindly try to decode it
	int revieveBufferPos = 0;
	position theirWindow = decodeWindowSize(receivingBuffer, revieveBufferPos);

	position useWindow;
	useWindow.x = min(theirWindow.x, width);
	useWindow.y = min(theirWindow.y, height);

	//now that we have an agreed upon window size, lets set up the player enviornment

	stopHandler::setContrlCHandler(&handleStop);//register the handler for ctrl c
	//Microsoft visual c++ compiler does not allow arrays to be defined with variables :'(
	char * prevScreen = (char *)malloc(height*width*sizeof(char));
	//y, x
	char * screen = (char *)malloc(height*width*sizeof(char));
	//initialize the screen with emptyness
	for(int i=0;i<width;i++){
		for(int j=0;j<height;j++){
			screen[j*width+i] = '.';
		}
	}

	//fill the screen with white so that any un playable space will be knwon
	setBackgroundColor(WHITE);
	gotoxy(0,0);
	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			cout << " ";
		}
	}
	resetColor();

	int heading =0;
	position apple;
	apple.x=5;
	apple.y=5;
	vector<position> snake;
	position p;
	p.x=width/2;
	p.y=height/2;
	snake.push_back(p);

	screen[apple.y*width+apple.x] = 'A';
	screen[p.y*width+p.x] = 'S';
	cls();
	//hidecursor();
	volatile CursorHider hideCursor;//automatically hides the cursor and unhides when deallocated
	#ifdef _WIN32
		DWORD myThreadID;
		CreateThread(0, 0, winThread, &heading, 0, &myThreadID);
	#else
		pthread_t inputThreadObject;
		pthread_create(&inputThreadObject, nullptr,inputThread,(void *)&heading);
	#endif
	while(gameRunning){
		if(paused){
			msleep(20);
			continue;
		}

		render(screen,prevScreen);
		//Remove the old snake from the screen
		for(size_t i=0;i<snake.size();i++){
			screen[snake[i].y*width+snake[i].x] = '.';
		}

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
		}//check to see if the snake has collided to its self
		for(size_t i=0;i<snake.size();i++){
			if(snake[i].x == sp.x && snake[i].y == sp.y){
				gameRunning=false;
				break;
			}
		}

		//if the head is on the apple
		if(snake[0].x == apple.x && snake[0].y == apple.y){
			position np;
			snake.push_back(np);
			bool notValid = true;
			while(notValid){
				apple.x = rand()%(width-5)+3;
				apple.y = rand()%(height-5)+3;
				notValid=false;
				for(size_t i=0;i<snake.size();i++){
					if(snake[i].x == apple.x && snake[i].y == apple.y){
						notValid=true;
						break;
					}
				}
			}
			screen[apple.y*width+apple.x]='A';
		}

		tmp = snake[0];
		snake[0] = sp;
		for(size_t i=1;i<snake.size();i++){
			position tmp2 = tmp;
			tmp = snake[i];
			snake[i]=tmp2;
		}



		//add the new snake to the screen
		for(size_t i=0;i<snake.size();i++){
			screen[snake[i].y*width+snake[i].x] = 'S';
		}
		if(sp.x<=0 || sp.x >=width || sp.y <= 0 || sp.y >= height){
			gameRunning = false;
		}
		msleep((heading % 2 ==0)?60:35);
	}

	resetColor();
	showcursor();
	gotoxy(2,height -2);
	cout << "GAME OVER!! Score:" <<snake.size() << endl;
	//cout << snake[0].x <<" " << snake[0].y << endl;
	showcursor();
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
	for(int x=0;x<width;x++){
		for(int y=0;y<height;y++){
			int index = x + y*width;
			if(current[index] != prev[index]){
				prev[index] = current[index];
				switch(current[index]){
					case 'S'://your snake
						gotoxy(x,y);
						//set the color to green by default but make is yellow or brown when it gets close to the edge
						if(x==0 || x == width-1 || y == 0 || y == height-1){
							setBackgroundColor(BROWN);
						}else if(x <= 2 || x >= width-3 || y <= 3 || y >= height-3){
							setBackgroundColor(YELLOW);
						}else{
							setBackgroundColor(GREEN);
						}
						//re wright this char
						cout << " ";
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