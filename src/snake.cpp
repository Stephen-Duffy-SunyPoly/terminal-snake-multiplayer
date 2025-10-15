#include <iostream>
#include "rogueutil.h"
#include "stopHandler.h"
#include <vector>
#include <cstdbool>
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

int main() {
	width = tcols();
	height = trows();
	//terminal size check;
	if(width < 30 || height < 30){
		cout << "Terminal Window too Small!!" << endl;
		return EXIT_FAILURE;
	}
	saveDefaultColor();//save current terminal colors
	stopHandler::setContrlCHandler(&handleStop);//register the handler for ctrl c
	setConsoleTitle("SNAKE!!");

	//Microsoft visual c++ compiler does not allow arrays to be defined with variables

	char * prevScreen = (char *)malloc(height*width*sizeof(char));

	//y, x
	char * screen = (char *)malloc(height*width*sizeof(char));
	for(int i=0;i<width;i++){
		for(int j=0;j<height;j++){
			screen[j*width+i] = '.';
		}
	}

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
	hidecursor();
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
		position sp;
		position tmp;
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

}

void render(char current[] , char prev[]){
	for(int x=0;x<width;x++){
		for(int y=0;y<height;y++){
			int index = x + y*width;
			if(current[index] != prev[index]){
				prev[index] = current[index];
				switch(current[index]){
					case 'S':
						gotoxy(x,y);
						if(x==0 || x == width-1 || y == 0 || y == height-1){
							setBackgroundColor(BROWN);
						}else if(x <= 2 || x >= width-3 || y <= 3 || y >= height-3){
							setBackgroundColor(YELLOW);
						}else{
							setBackgroundColor(GREEN);
						}
						cout << " ";
						break;
					case 'A':
						gotoxy(x,y);
						setBackgroundColor(RED);
						cout << " ";
						break;
					case '.':
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
	return nullptr;
}

#ifdef _WIN32
	DWORD WINAPI winThread(LPVOID params){
		inputThread((void* )params);
		return 0;
	}
#endif
