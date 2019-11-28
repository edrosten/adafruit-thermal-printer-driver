#include <cups/raster.h>
#include <iostream>
#include <vector>
#include <random>
#include <array>
#include <utility>
#include <fcntl.h>
#include <signal.h>

using std::clog;
using std::cerr;
using std::cout;
using std::endl;
using std::max;
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


// Convenience function to build an array
template<typename... T>
auto arr(T&&... t) -> std::array<unsigned char, sizeof...(T)>{
	return {{t...}};
}

inline constexpr unsigned char operator""_uc(unsigned long long arg) noexcept {
    return static_cast<unsigned char >(arg);
}


array<unsigned char, 2> binary(uint16_t n){
	return {{static_cast<unsigned char>(n&0xff), static_cast<unsigned char>(n >> 8)}};
}


auto printerInitializeCommand = arr(ESC, 0x40_uc);

// enter raster mode and set up x and y dimensions
void rasterheader(uint16_t xsize, uint16_t ysize)
{
	// Page 33 of the manual
	cout << GS << 'v' << '0' << '\0' << binary(xsize) << binary(ysize);
}


void skip_vertical_space(double centimeters){
	double inches = centimeters / 2.54;
	double DPI=202.3;
	int lines = inches * DPI + .5;
	
	cout << ESC << 'J' << (char)(max(lines, 255));
}


// sent at the very end of print job
void shutdown()
{
	cout << printerInitializeCommand;
}

// sent at the end of every page
__sighandler_t old_signal;
void end_page()
{
	//skip_vertical_space(2.5);
	cout << "\n\n\n\n\n\n\n\n";
	signal(15,old_signal);
}

// sent on job canceling
void cancel_job(int)
{
	//FIXME
	//Need to break into line chunks to allow for clean cancellation
	for(int i=0;i<0x258;++i)
		cout << (char)0;
	end_page();
	shutdown();
}

// invoked before starting to print a page
void pageSetup()
{
	old_signal = signal(15,cancel_job);
}


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
		
		
		clog << "Line bytes: " << buffer.size() << endl;
		pageSetup();
		//cout << printerInitializeCommand;

		// Jarvis, Judice, and Ninke error diffusion
		constexpr array<array<int, 5>, 3> diffusion = {{
			{{0, 0, 0, 7, 5}},
			{{3, 5, 7, 5, 3}},
			{{1, 3, 5, 3, 1}}
		}};
		constexpr double diffusion_divisor=42;
		
		// Buffer for error diffusion
		vector<vector<double>> errors(diffusion.size(), vector<double>(buffer.size(), 0.0));

		/* read raster data */
		for (unsigned int y = 0; y < header.cupsHeight; y ++)
		{
			if (cupsRasterReadPixels(ras, buffer.data(), header.cupsBytesPerLine) == 0)
				break;

			//Print in MSB format
			rasterheader(header.cupsWidth/8, 1);
			unsigned char current=0;
			int bits=0;

			for(int i=0; i < (int)buffer.size(); i++){

				double pixel = buffer[i] + errors[0][i];
				double actual = pixel>128?255:0;

				double error = pixel - actual; //This error is then distributed

				//Diffuse forward the errors	
				for(int r=0; r < (int)diffusion.size(); r++)
					for(int cc=0; cc < (int)diffusion[0].size(); cc++){
						int c = cc - diffusion[0].size()/2;
						if(c+i >= 0 && c+i < (int)buffer.size() && diffusion[r][cc]){
							errors[r][i+c] += error * diffusion[r][cc] / diffusion_divisor;
						}
					}



				//Set bit corresponds to printing a dot, i.e. dark
				current |= (actual==0)<<(7-bits);
				bits++;
				if(bits == 8){
					cout << current << std::flush;
					bits = 0;
					current = 0;
				}
			}
			if(bits)
				cout << current;


			errors.erase(errors.begin());
			errors.push_back(vector<double>(buffer.size(), 0.0));
				
			/* write raster data to printer on stdout */
		}

		end_page();
		/* finish this page */
	}

	cupsRasterClose(ras);
	//shutdown();

}
