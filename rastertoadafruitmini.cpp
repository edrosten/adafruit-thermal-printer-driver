#include <cups/raster.h>

#include <iostream>
#include <vector>
#include <array>
#include <utility>
#include <cmath>
#include <algorithm>

using std::clog;
using std::cout;
using std::endl;
using std::vector;
using std::array;


constexpr unsigned char ESC = 0x1b;
constexpr unsigned char GS = 0x1d;

// Write out a std::array of bytes as bytes.  This will form the basis
// of sending data to the printer.
template<size_t N>
std::ostream& operator<<(std::ostream& out, const array<unsigned char, N>& a){
	out.write(reinterpret_cast<const char*>(a.data()), a.size());
	return out;
}

array<unsigned char, 2> binary(uint16_t n){
	return {{static_cast<unsigned char>(n&0xff), static_cast<unsigned char>(n >> 8)}};
}


void printerInitialise(){
	cout << ESC << '\x40';
}

// enter raster mode and set up x and y dimensions
void rasterheader(uint16_t xsize, uint16_t ysize)
{
	// Page 33 of the manual
	// The x size is the number of bytes per row, so the number of pixels
	// is always a multiple of 8
	cout << GS << 'v' << '0' << '\0' << binary((xsize+7)/8) << binary(ysize);
}


constexpr array<array<int, 5>, 3> diffusion_coefficients = {{
		{{0, 0, 0, 7, 5}},
		{{3, 5, 7, 5, 3}},
		{{1, 3, 5, 3, 1}}
}};
constexpr double diffusion_divisor=42;


int main(){

	cups_raster_t *ras = cupsRasterOpen(0, CUPS_RASTER_READ);
	cups_page_header2_t header;
	int page = 0;

	while (cupsRasterReadHeader2(ras, &header))
	{
		/* setup this page */
		page ++;
		clog << "PAGE: " << page << " " << header.NumCopies << "\n";
		clog << "BPP: " << header.cupsBitsPerPixel << endl;
		clog << "BitsPerColor: " << header.cupsBitsPerColor << endl;
		clog << "Width: " << header.cupsWidth << endl;
		clog << "Height: " << header.cupsHeight << endl;

		// Input data buffer for one line
		vector<unsigned char> buffer(header.cupsBytesPerLine);
		
		//Error diffusion data
		vector<vector<double>> errors(diffusion_coefficients.size(), vector<double>(buffer.size(), 0.0));

		clog << "Line bytes: " << buffer.size() << endl;
		printerInitialise();

		/* read raster data */
		for (unsigned int y = 0; y < header.cupsHeight; y ++)
		{
			if (cupsRasterReadPixels(ras, buffer.data(), header.cupsBytesPerLine) == 0)
				break;

			//Print in MSB format, one line at a time
			rasterheader(header.cupsWidth, 1);
			unsigned char current=0;
			int bits=0;

			for(int i=0; i < (int)buffer.size(); i++){
				
				//The actual pixel value with gamma correction
				double pixel = pow(buffer[i]/255., 1./2.2) + errors[0][i];
				double actual = pixel>.5?1:0;
				double error = pixel - actual; //This error is then distributed


				//Diffuse forward the error	
				for(int r=0; r < (int)diffusion_coefficients.size(); r++)
					for(int cc=0; cc < (int)diffusion_coefficients[0].size(); cc++){
						int c = cc - diffusion_coefficients[0].size()/2;
						if(c+i >= 0 && c+i < (int)buffer.size() && diffusion_coefficients[r][cc]){
							errors[r][i+c] += error * diffusion_coefficients[r][cc] / diffusion_divisor;
						}
					}

				current |= (pixel<0.5)<<(7-bits);
				bits++;
				if(bits == 8){
					cout << current;
					bits = 0;
					current = 0;
				}
			}
			if(bits)
				cout << current;

			
			//Roll the buffer round.
			std::rotate(errors.begin(), errors.begin()+1, errors.end());
			for(auto& p:errors.back())
				p=0;
			
	
		}

		/* finish this page */
	}
	cout << "\n\n\n";
	cupsRasterClose(ras);
}
