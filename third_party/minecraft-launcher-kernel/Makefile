CXX = g++
AR = ar
CXXFLAGS = -Wall -Wextra -O2 -std=c++17
QT_DIR = E:/Qt/6.11.1/mingw_64
QT_CXXFLAGS = -I$(QT_DIR)/include -I$(QT_DIR)/include/QtCore -I$(QT_DIR)/include/QtNetwork -I$(QT_DIR)/include/QtZlib
QT_LDFLAGS = -L$(QT_DIR)/lib -lQt6Core -lQt6Network
COMMON_LDFLAGS = -mconsole

LIB_DIR = libmcbase
LIB_INC = $(LIB_DIR)/include
LIB_SRC = $(LIB_DIR)/src
LIB_OBJ_DIR = $(LIB_DIR)/src

LIB_SRC_FILES = \
	$(LIB_SRC)/mc_i18n.cpp \
	$(LIB_SRC)/mc_log.cpp \
	$(LIB_SRC)/mc_str.cpp \
	$(LIB_SRC)/mc_path.cpp \
	$(LIB_SRC)/mc_hash.cpp \
	$(LIB_SRC)/mc_http.cpp \
	$(LIB_SRC)/mc_zip.cpp \
	$(LIB_SRC)/mc_download.cpp \
	$(LIB_SRC)/mc_download_qt.cpp \
	$(LIB_SRC)/mc_version.cpp \
	$(LIB_SRC)/mc_library.cpp \
	$(LIB_SRC)/mc_manifest.cpp \
	$(LIB_SRC)/mc_mod.cpp

LIB_OBJ = $(LIB_SRC_FILES:.cpp=.o)
LIB_A = libmcbase.a

MCJAVA_PRIV = mcjava/private
MCJAVA_PRIV_SRC = $(MCJAVA_PRIV)/mc_java.cpp
MCJAVA_PRIV_OBJ = $(MCJAVA_PRIV_SRC:.cpp=.o)

LOGIN_PRIV = login/private
LOGIN_PRIV_SRC = $(LOGIN_PRIV)/mc_auth.cpp $(LOGIN_PRIV)/mc_auth_msa.cpp
LOGIN_PRIV_OBJ = $(LOGIN_PRIV_SRC:.cpp=.o)

DL_PRIV = downloader/private
DL_PRIV_SRC = $(DL_PRIV)/mc_asset.cpp $(DL_PRIV)/mc_java_dl.cpp
DL_PRIV_OBJ = $(DL_PRIV_SRC:.cpp=.o)

.PHONY: all clean

all: mcver.exe downloader.exe mcjava.exe mclaunch.exe mcsearch.exe login.exe installer.exe modsearch.exe modver.exe

$(LIB_A): $(LIB_OBJ)
	$(AR) rcs $@ $^

$(LIB_SRC)/%.o: $(LIB_SRC)/%.cpp
	$(CXX) $(CXXFLAGS) -I$(LIB_INC) $(QT_CXXFLAGS) -c -o $@ $<

$(MCJAVA_PRIV)/%.o: $(MCJAVA_PRIV)/%.cpp
	$(CXX) $(CXXFLAGS) -I$(LIB_INC) -I$(MCJAVA_PRIV)/include $(QT_CXXFLAGS) -c -o $@ $<

$(LOGIN_PRIV)/%.o: $(LOGIN_PRIV)/%.cpp
	$(CXX) $(CXXFLAGS) -I$(LIB_INC) -I$(LOGIN_PRIV)/include $(QT_CXXFLAGS) -c -o $@ $<

$(DL_PRIV)/%.o: $(DL_PRIV)/%.cpp
	$(CXX) $(CXXFLAGS) -I$(LIB_INC) -I$(DL_PRIV)/include $(QT_CXXFLAGS) -c -o $@ $<

# ---- mcver.exe ----
mcver.exe: mcver/main.cpp $(LIB_A)
	$(CXX) $(CXXFLAGS) -I$(LIB_INC) $(QT_CXXFLAGS) -c -o mcver_main.o mcver/main.cpp
	$(CXX) -o $@ mcver_main.o $(LIB_A) $(QT_LDFLAGS) $(COMMON_LDFLAGS)

# ---- mcsearch.exe ----
mcsearch.exe: mcsearch/main.cpp $(LIB_A)
	$(CXX) $(CXXFLAGS) -I$(LIB_INC) $(QT_CXXFLAGS) -c -o mcsearch_main.o mcsearch/main.cpp
	$(CXX) -o $@ mcsearch_main.o $(LIB_A) $(QT_LDFLAGS) $(COMMON_LDFLAGS)

# ---- mcjava.exe ----
mcjava.exe: mcjava/main.cpp $(MCJAVA_PRIV_OBJ) $(LIB_A)
	$(CXX) $(CXXFLAGS) -I$(LIB_INC) -I$(MCJAVA_PRIV)/include $(QT_CXXFLAGS) -c -o mcjava_main.o mcjava/main.cpp
	$(CXX) -o $@ mcjava_main.o $(MCJAVA_PRIV_OBJ) $(LIB_A) $(QT_LDFLAGS) $(COMMON_LDFLAGS)

# ---- downloader.exe ----
downloader.exe: downloader/main.cpp $(DL_PRIV_OBJ) $(LIB_A)
	$(CXX) $(CXXFLAGS) -I$(LIB_INC) -I$(DL_PRIV)/include $(QT_CXXFLAGS) -c -o downloader_main.o downloader/main.cpp
	$(CXX) -o $@ downloader_main.o $(DL_PRIV_OBJ) $(LIB_A) $(QT_LDFLAGS) $(COMMON_LDFLAGS)

# ---- login.exe ----
login.exe: login/main.cpp $(LOGIN_PRIV_OBJ) $(LIB_A)
	$(CXX) $(CXXFLAGS) -I$(LIB_INC) -I$(LOGIN_PRIV)/include $(QT_CXXFLAGS) -c -o login_main.o login/main.cpp
	$(CXX) -o $@ login_main.o $(LOGIN_PRIV_OBJ) $(LIB_A) $(QT_LDFLAGS) $(COMMON_LDFLAGS)

# ---- installer.exe ----
installer.exe: installer/main.cpp $(LIB_A)
	$(CXX) $(CXXFLAGS) -I$(LIB_INC) $(QT_CXXFLAGS) -c -o installer_main.o installer/main.cpp
	$(CXX) -o $@ installer_main.o $(LIB_A) $(QT_LDFLAGS) $(COMMON_LDFLAGS)

# ---- mclaunch.exe ----
mclaunch.exe: mclaunch/main.cpp $(LOGIN_PRIV_OBJ) $(LIB_A)
	$(CXX) $(CXXFLAGS) -I$(LIB_INC) -I$(LOGIN_PRIV)/include $(QT_CXXFLAGS) -c -o mclaunch_main.o mclaunch/main.cpp
	$(CXX) -o $@ mclaunch_main.o $(LOGIN_PRIV_OBJ) $(LIB_A) $(QT_LDFLAGS) $(COMMON_LDFLAGS)

# ---- modsearch.exe ----
modsearch.exe: modsearch/main.cpp $(LIB_A)
	$(CXX) $(CXXFLAGS) -I$(LIB_INC) $(QT_CXXFLAGS) -c -o modsearch_main.o modsearch/main.cpp
	$(CXX) -o $@ modsearch_main.o $(LIB_A) $(QT_LDFLAGS) $(COMMON_LDFLAGS)

# ---- modver.exe ----
modver.exe: modver/main.cpp $(LIB_A)
	$(CXX) $(CXXFLAGS) -I$(LIB_INC) $(QT_CXXFLAGS) -c -o modver_main.o modver/main.cpp
	$(CXX) -o $@ modver_main.o $(LIB_A) $(QT_LDFLAGS) $(COMMON_LDFLAGS)

clean:
	-del /Q $(subst /,\,$(LIB_OBJ)) 2>NUL
	-del /Q $(subst /,\,$(MCJAVA_PRIV_OBJ)) 2>NUL
	-del /Q $(subst /,\,$(LOGIN_PRIV_OBJ)) 2>NUL
	-del /Q $(subst /,\,$(DL_PRIV_OBJ)) 2>NUL
	-del /Q $(LIB_A) 2>NUL
	-del /Q mcver_main.o downloader_main.o mcjava_main.o mclaunch_main.o mcsearch_main.o login_main.o installer_main.o modsearch_main.o modver_main.o 2>NUL
	-del /Q mcver.exe downloader.exe mcjava.exe mclaunch.exe mcsearch.exe login.exe installer.exe modsearch.exe modver.exe 2>NUL
	-del /Q test_*.exe 2>NUL
