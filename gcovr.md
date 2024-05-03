The following steps allow you generate an visualize code coverage report upon running the unittest.
1. Install the gcovr.
```shell
pip install gcovr
```
2. Build the unittest via CMake.
```shell
mkdir build
cmake -B ./build && cmake --build build
```
3. Run the unittest.
```shell
cd build && ./cfuture_test
```
4. Generate the html pages of the code coverage report. Please make sure that you are running `gcovr` under the `build` directory.
```shell
cd build
gcovr -r ../ . --html --html-details -o coverage.html --exclude='.*googletest.*'
```
5. Open `build/coverage.html` with web browser, and check the result.
