# psd_sdk
A C++ library that directly reads Photoshop PSD files. The library supports:
* Groups
* Nested layers
* Smart Objects
* User and vector masks
* Transparency masks and additional alpha channels
* 8-bit, 16-bit, and 32-bit data in grayscale and RGB color mode
* All compression types known to Photoshop

Additionally, limited export functionality is also supported.

For more information, please go to: https://molecular-matters.com/products_psd_sdk.html

## Directory structure
### bin
Contains a Photoshop PSD file used by the sample code.

### build
Contains Visual Studio projects and solutions for VS 2008, 2010, 2012, 2013, 2015, 2017, and 2019.

### src
Contains the library source code as well as a sample application that shows how to use the SDK in order to read and write PSD files.

