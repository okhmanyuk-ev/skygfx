mkdir build
cd build

if [ "$(uname)" == "Darwin" ]; then
    cmake .. -G Xcode
else
    cmake ..
    $SHELL
fi