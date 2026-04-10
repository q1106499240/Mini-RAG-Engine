#include "prompt.h"

using namespace std;

string build_prompt(
	const string& context,
	const string& question) {
	
	string prompt =
		"Answer the question using the context below.\n\n"
		"Context:\n" + context + "\n\n"
		"Question:\n" + question + "\n\n"
		"Answer:";
	return prompt;
}