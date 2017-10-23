
#include <string>
#include <tuple>
#include <vector>
#include <iostream>
#include <locale>
#include <codecvt>
//#include "../gbdt_prediction.cpp"
//#include "./idf_index.cpp"
using namespace std;
std::u32string conv(const std::string& input){
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
    return converter.from_bytes(input.c_str());
}
void tokenize(const std::string& input) {
  wcout.imbue(std::locale(""));
  std::string context = u8"映画は苦手だったのですが、母の誘いで見に行きました。後半部分が良いです。";  
  std::wstring_convert<std::codecvt_utf8<wchar_t>,wchar_t> cv;
  std::wstring wsmessage = cv.from_bytes(context);
  wcout << wsmessage << endl;
  std::vector<std::string> contain;
  int size = contain.size();
  for(auto c:wsmessage) cout << cv.to_bytes(c) << " ";
  // slicing
}

int main() {
  cout << "test" << endl;
  tokenize("");
}
