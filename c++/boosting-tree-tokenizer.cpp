
#include <string>
#include <tuple>
#include <vector>
#include <iostream>
#include <locale>
#include <codecvt>
//#include "../gbdt_prediction.cpp"
//#include "idf_index.cpp"
#include "./src/application/predictor.hpp"

void tokenize(const std::string& input) {
  int maxIndex = 0;
  //for(auto [key, val]:idf_index) 
  //  if( val > maxIndex ) maxIndex = val;
  std::cout << "maxIndex: " << maxIndex << std::endl; 

  std::wcout.imbue(std::locale(""));
  std::string context = u8"映画は苦手だったのですが、母の誘いで見に行きました。後半部分が良いです。";  
  std::wstring_convert<std::codecvt_utf8<wchar_t>,wchar_t> cv;
  std::wstring wsmessage = cv.from_bytes(context);
  std::vector<std::string> contain;
  for(auto c:wsmessage) {
    contain.push_back(cv.to_bytes(c));
    std::cout << cv.to_bytes(c) << "";
  }
  std::cout << std::endl;
  // slicing
  int size = contain.size();
  for(int i = 0; i < size - 9; i++ ) {
    std::vector<double> inserts(maxIndex);  
    double result = 0.0;
    for(int j=i; j < i + 10; j++ ) {
      std::string key = std::to_string(j-i) + contain[j];
      std::cout << "sample " << key << std::endl;
      //int cursol = idf_index[key];
      //inserts[cursol] = 1.0;
    }
    //std::cout << "PREDICT: " << predict(&inserts[0]) << std::endl;
  }
}

int main() {
  std::cout << "test" << std::endl;
  tokenize("");
}
