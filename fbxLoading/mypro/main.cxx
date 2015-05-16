#include <stdlib.h>
#include <math.h>

#include<time.h>

#include "FBXLoader.h"

#include <GL/glut.h>


// angle of rotation for the camera direction
float angle = 0.0f;

// actual vector representing the camera's direction
float lx=0.0f,lz=-1.0f;

// XZ position of the camera
float x=0.0f, z=800.0f;

// the key states. These variables will be zero
//when no key is being presses
float deltaAngle = 0.0f;
float deltaMove = 0;
int xOrigin = -1;

//globals
FBXLoader * gSceneContext;

//memory alocator for our nodes ...
class MyMemoryAllocator
{
public:
	static void* MyMalloc(size_t pSize)
	{
		char *p = (char*)malloc(pSize+1);
		*p = '#';
		return p+1;
	}

	static void* MyCalloc(size_t pCount, size_t pSize)
	{
		char *p = (char*)calloc(pCount, pSize+1);
		*p = '#';
		return p+1;
	}

	static void* MyRealloc(void* pData, size_t pSize)
	{
		if (pData)
		{
			FBX_ASSERT(*((char*)pData-1)=='#');
			if (*((char*)pData-1)=='#')
			{
				char *p = (char*)realloc((char*)pData-1, pSize+1);
				*p = '#';
				return p+1;
			}
			else
			{   // Mismatch
				char *p = (char*)realloc((char*)pData, pSize+1);
				*p = '#';
				return p+1;
			}
		}
		else
		{
			char *p = (char*)realloc(NULL, pSize+1);
			*p = '#';
			return p+1;
		}
	}

	static void MyFree(void* pData)
	{
		if (pData==NULL)
			return;
		FBX_ASSERT(*((char*)pData-1)=='#');
		if (*((char*)pData-1)=='#')
		{
			free((char*)pData-1);
		}
		else
		{   // Mismatch
			free(pData);
		}
	}
};



// Trigger the display of the current frame.
void TimerCallback(int)
{
	// Ask to display the current frame only if necessary.
	//if (gSceneContext->GetStatus() == SceneContext::MUST_BE_REFRESHED)
	//{
	//	glutPostRedisplay();
	//}

	gSceneContext->OnTimerClick();

	// Call the timer to display the next frame.
	glutTimerFunc((unsigned int)gSceneContext->GetFrameTime().GetMilliSeconds(), TimerCallback, 0);
}


void init()
{

	GLfloat light_ambient[] =
	{0.2, 0.2, 0.2, 1.0};
	GLfloat light_diffuse[] =
	{1.0, 1.0, 1.0, 1.0};
	GLfloat light_specular[] =
	{1.0, 1.0, 1.0, 1.0};
	GLfloat light_position[] =
	{1.0, 1.0, 1.0, 0.0};

	glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
	glLightfv(GL_LIGHT0, GL_SPECULAR, light_specular);
	glLightfv(GL_LIGHT0, GL_POSITION, light_position);

	glEnable(GL_LIGHT0);
	glDepthFunc(GL_LESS);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LIGHTING);



	//to do to load model..
	gSceneContext->LoadFile();

	//choose spicefic animation
	//animations in the fbx file array from 0 to --> no of animations..
	gSceneContext->SetCurrentAnimStack(0); //Animation Array Selection..

	//we need timer to calculate frame time..
	// Call the timer to display the first frame.
	glutTimerFunc((unsigned int)gSceneContext->GetFrameTime().GetMilliSeconds(), TimerCallback, 0);


}


void changeSize(int w, int h) {

	// Prevent a divide by zero, when window is too short
	// (you cant make a window of zero width).
	if (h == 0)
		h = 1;

	float ratio =  w * 1.0 / h;

	// Use the Projection Matrix
	glMatrixMode(GL_PROJECTION);

	// Reset Matrix
	glLoadIdentity();

	// Set the viewport to be the entire window
	glViewport(0, 0, w, h);

	// Set the correct perspective.
	gluPerspective(60.0f, ratio, 10.0f, 10000.0f);

	// Get Back to the Modelview
	glMatrixMode(GL_MODELVIEW);
}



void computePos(float deltaMove) {

	x += deltaMove * lx * 0.1f;
	z += deltaMove * lz * 0.1f;
}

void renderScene(void)   //to do render here..
{


	if (deltaMove)
		computePos(deltaMove);

	// Clear Color and Depth Buffers
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Reset transformations
	glLoadIdentity();
	// Set the camera
	gluLookAt(x, 200.0f, z,
		x+lx, 200.0f,  z+lz,
		0.0f, 1.0f,  0.0f);


	glScalef(0.2,0.2,0.2);
	gSceneContext->Draw();   //<<----------draw from library..
	glutSwapBuffers();
} 

void processNormalKeys(unsigned char key, int xx, int yy) { 	

	if (key == 27)
		exit(0);
} 

void pressKey(int key, int xx, int yy) {

	switch (key) {
	case GLUT_KEY_UP : deltaMove = 0.5f; break;
	case GLUT_KEY_DOWN : deltaMove = -0.5f; break;
	}
} 

void releaseKey(int key, int x, int y) { 	

	switch (key) {
	case GLUT_KEY_UP :
	case GLUT_KEY_DOWN : deltaMove = 0;break;
	}
} 

void mouseMove(int x, int y) { 	

	// this will only be true when the left button is down
	if (xOrigin >= 0) {

		// update deltaAngle
		deltaAngle = (x - xOrigin) * 0.001f;

		// update camera's direction
		lx = sin(angle + deltaAngle);
		lz = -cos(angle + deltaAngle);
	}
}

void mouseButton(int button, int state, int x, int y) {

	// only start motion if the left button is pressed
	if (button == GLUT_LEFT_BUTTON) {

		// when the button is released
		if (state == GLUT_UP) {
			angle += deltaAngle;
			xOrigin = -1;
		}
		else  {// state = GLUT_DOWN
			xOrigin = x;
		}
	}
}


bool InitializeOpenGL()
{
	// Initialize GLEW.
	GLenum lError = glewInit();
	if (lError != GLEW_OK)
	{
		FBXSDK_printf("GLEW Error: %s\n", glewGetErrorString(lError));
		return false;
	}

	//glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glClearColor(0.0, 0.0, 0.0, 0.0);

	// OpenGL 1.5 at least.
	if (!GLEW_VERSION_1_5)
	{
		FBXSDK_printf("The OpenGL version should be at least 1.5 to display shaded scene!\n");
		return false;
	}

	return true;
}

int main(int argc, char **argv) {


	// Use a custom memory allocator
	FbxSetMallocHandler(MyMemoryAllocator::MyMalloc);
	FbxSetReallocHandler(MyMemoryAllocator::MyRealloc);
	FbxSetFreeHandler(MyMemoryAllocator::MyFree);
	FbxSetCallocHandler(MyMemoryAllocator::MyCalloc);



	// init GLUT and create window
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA);
	glutInitWindowPosition(100,100);
	glutInitWindowSize(800,600);
	glutCreateWindow("FBX Loading");

	// Initialize OpenGL.
	const bool lSupportVBO = InitializeOpenGL();

	FbxString lFilePath("zombii.FBX");

	gSceneContext = new FBXLoader(lFilePath, 800, 600);



	init();
	// register callbacks
	glutDisplayFunc(renderScene);
	glutReshapeFunc(changeSize);
	glutIdleFunc(renderScene);

	glutIgnoreKeyRepeat(1);
	glutKeyboardFunc(processNormalKeys);
	glutSpecialFunc(pressKey);
	glutSpecialUpFunc(releaseKey);

	// here are the two new functions
	glutMouseFunc(mouseButton);
	glutMotionFunc(mouseMove);

	// OpenGL init
	glEnable(GL_DEPTH_TEST);

	// enter GLUT event processing cycle
	glutMainLoop();

	return 1;
}