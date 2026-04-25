#include <QtTest/QtTest>

int runCryptoTests(int argc, char** argv);
int runFormatTests(int argc, char** argv);
int runGraphMockTests(int argc, char** argv);
int runGuiSmoke(int argc, char** argv);

int main(int argc, char** argv) {
  int rc = 0;
  rc |= runCryptoTests(argc, argv);
  rc |= runFormatTests(argc, argv);
  rc |= runGraphMockTests(argc, argv);
  rc |= runGuiSmoke(argc, argv);
  return rc;
}
