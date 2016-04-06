GLFW_CFLAGS = -I/opt/local/include
GLFW_LDFLAGS = -L/opt/local/lib -lglfw

CFLAGS = $(GLFW_CFLAGS) -framework OpenGL -framework OpenCL
LDFLAGS = $(GLFW_LDFLAGS)

default: example
