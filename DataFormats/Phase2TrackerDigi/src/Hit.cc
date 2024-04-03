#include "../interface/Hit.h"

//Describes a hit (meant for use in 4x4 sensor coordinates) by its row number, column number, and adc value
Hit::Hit(int row_num, int col_num, int adc_num) {

	row_ = row_num;
	col_ = col_num;
	adc_ = adc_num;

}
