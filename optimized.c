#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DATA_FILE "money_tracker.dat"
#define CATEGORY_SIZE 64
#define NOTE_SIZE 128
#define DATE_SIZE 20
#define INPUT_SIZE 256

typedef struct {
	char date[DATE_SIZE];
	char type;                 /* I = income, E = expense */
	int64_t cents;
	char category[CATEGORY_SIZE];
	char note[NOTE_SIZE];
} Transaction;

typedef struct {
	Transaction *items;
	size_t count;
	size_t capacity;
} Tracker;

static int read_line(const char *prompt, char *buffer, size_t size) {
	printf("%s", prompt);
	if (!fgets(buffer, (int)size, stdin)) return 0;
	size_t len = strlen(buffer);
	if (len && buffer[len - 1] == '\n') {
		buffer[len - 1] = '\0';
	} else {
		int ch;
		while ((ch = getchar()) != '\n' && ch != EOF) {}
	}
	return 1;
}

static int parse_money(const char *text, int64_t *cents) {
	const unsigned char *p = (const unsigned char *)text;
	uint64_t whole = 0, fraction = 0;
	int fractional_digits = 0;

	while (isspace(*p)) ++p;
	if (*p == '+') ++p;
	if (!isdigit(*p)) return 0;

	while (isdigit(*p)) {
		unsigned digit = (unsigned)(*p++ - '0');
		if (whole > ((uint64_t)INT64_MAX / 100 - digit) / 10) return 0;
		whole = whole * 10 + digit;
	}
	if (*p == '.') {
		++p;
		while (isdigit(*p)) {
			if (fractional_digits == 2) return 0;
			fraction = fraction * 10 + (unsigned)(*p++ - '0');
			++fractional_digits;
		}
		if (fractional_digits == 0) return 0;
	}
	while (isspace(*p)) ++p;
	if (*p != '\0') return 0;
	if (fractional_digits == 1) fraction *= 10;
	if (whole > ((uint64_t)INT64_MAX - fraction) / 100) return 0;
	*cents = (int64_t)(whole * 100 + fraction);
	return *cents > 0;
}

static void print_money(int64_t cents) {
	printf("$%" PRId64 ".%02" PRId64, cents / 100, cents % 100);
}

static int has_separator(const char *text) {
	return strchr(text, '|') != NULL;
}

static int reserve_one(Tracker *tracker) {
	if (tracker->count < tracker->capacity) return 1;
	size_t next = tracker->capacity ? tracker->capacity * 2 : 16;
	if (next < tracker->capacity || next > SIZE_MAX / sizeof(*tracker->items)) return 0;
	Transaction *items = realloc(tracker->items, next * sizeof(*items));
	if (!items) return 0;
	tracker->items = items;
	tracker->capacity = next;
	return 1;
}
// Money Tracker

static int save_tracker(const Tracker *tracker) {
	FILE *file = fopen(DATA_FILE, "w");
	if (!file) return 0;
	for (size_t i = 0; i < tracker->count; ++i) {
		const Transaction *t = &tracker->items[i];
		if (fprintf(file, "%s|%c|%" PRId64 "|%s|%s\n", t->date, t->type,
					t->cents, t->category, t->note) < 0) {
			fclose(file);
			return 0;
		}
	}
	return fclose(file) == 0;
}

static void load_tracker(Tracker *tracker) {
	FILE *file = fopen(DATA_FILE, "r");
	if (!file) return;
	char line[DATE_SIZE + CATEGORY_SIZE + NOTE_SIZE + 64];
	while (fgets(line, sizeof(line), file)) {
		Transaction t = {0};
		int fields = sscanf(line, "%19[^|]|%c|%" SCNd64 "|%63[^|]|%127[^\n]",
							t.date, &t.type, &t.cents, t.category, t.note);
		size_t note_len = strlen(t.note);
		if (note_len && t.note[note_len - 1] == '\r') t.note[note_len - 1] = '\0';
		if (fields != 5 || (t.type != 'I' && t.type != 'E') || t.cents <= 0) continue;
		if (!reserve_one(tracker)) {
			fprintf(stderr, "Not enough memory to load saved transactions.\n");
			break;
		}
		tracker->items[tracker->count++] = t;
	}
	fclose(file);
}

static void add_transaction(Tracker *tracker) {
	Transaction t = {0};
	char type[INPUT_SIZE], amount[INPUT_SIZE];

	if (!read_line("Type (I = income, E = expense): ", type, sizeof(type))) return;
	if (strlen(type) != 1 || (toupper((unsigned char)type[0]) != 'I' &&
							 toupper((unsigned char)type[0]) != 'E')) {
		puts("Please enter I or E.");
		return;
	}
	t.type = (char)toupper((unsigned char)type[0]);
	if (!read_line("Amount (e.g. 12.50): ", amount, sizeof(amount)) ||
		!parse_money(amount, &t.cents)) {
		puts("Invalid amount. Enter a positive amount with up to two decimals.");
		return;
	}
	if (!read_line("Category: ", t.category, sizeof(t.category)) ||
		t.category[0] == '\0' || has_separator(t.category)) {
		puts("Category is required and cannot contain '|'.");
		return;
	}
	if (!read_line("Note (optional): ", t.note, sizeof(t.note))) return;
	if (has_separator(t.note)) {
		puts("Note cannot contain '|'.");
		return;
	}
	if (t.note[0] == '\0') strcpy(t.note, "-");

	time_t now = time(NULL);
	struct tm *local = localtime(&now);
	if (!local || !strftime(t.date, sizeof(t.date), "%Y-%m-%d %H:%M", local)) {
		strcpy(t.date, "unknown");
	}
	if (!reserve_one(tracker)) {
		puts("Unable to allocate memory for the transaction.");
		return;
	}
	tracker->items[tracker->count++] = t;
	if (!save_tracker(tracker)) {
		--tracker->count;
		puts("Could not save transaction; it was not added.");
		return;
	}
	puts("Transaction saved.");
}

static void list_transactions(const Tracker *tracker) {
	if (tracker->count == 0) {
		puts("No transactions recorded.");
		return;
	}
	puts("\n#   Date             Type     Amount       Category         Note");
	puts("--------------------------------------------------------------------------");
	for (size_t i = 0; i < tracker->count; ++i) {
		const Transaction *t = &tracker->items[i];
		printf("%-3zu %-16s %-8s ", i + 1, t->date,
			   t->type == 'I' ? "Income" : "Expense");
		print_money(t->cents);
		printf("   %-16s %s\n", t->category, t->note);
	}
}

static void show_summary(const Tracker *tracker) {
	int64_t income = 0, expenses = 0;
	for (size_t i = 0; i < tracker->count; ++i) {
		int64_t *total = tracker->items[i].type == 'I' ? &income : &expenses;
		if (tracker->items[i].cents > INT64_MAX - *total) {
			puts("Summary exceeds supported numeric range.");
			return;
		}
		*total += tracker->items[i].cents;
	}
	printf("\nIncome:   "); print_money(income);
	printf("\nExpenses: "); print_money(expenses);
	printf("\nBalance:  ");
	if (income >= expenses) {
		print_money(income - expenses);
	} else {
		printf("-"); print_money(expenses - income);
	}
	putchar('\n');
}

int main(void) {
	Tracker tracker = {0};
	char choice[INPUT_SIZE];
	load_tracker(&tracker);

	puts("Money Tracker");
	for (;;) {
		puts("\n1. Add transaction\n2. List transactions\n3. View summary\n4. Exit");
		if (!read_line("Choose an option: ", choice, sizeof(choice))) break;
		if (strcmp(choice, "1") == 0) add_transaction(&tracker);
		else if (strcmp(choice, "2") == 0) list_transactions(&tracker);
		else if (strcmp(choice, "3") == 0) show_summary(&tracker);
		else if (strcmp(choice, "4") == 0) break;
		else puts("Invalid option. Choose 1–4.");
	}

	free(tracker.items);
	return 0;
}
 