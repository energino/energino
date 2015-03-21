/*
 * sma.h - Moving Average Filter
 */

#ifndef SMA_H
#define SMA_H

#include <stddef.h>
#include <assert.h>

class SMA {
public:
  SMA(unsigned int period) :
    period(period), window(new int[period]), head(NULL), tail(NULL),
        total(0) {
    assert(period >= 1);
  }
  ~SMA() {
    delete[] window;
  }

  // Adds a value to the average, pushing one out if nescessary
  void add(int val) {
    // Special case: Initialization
    if (head == NULL) {
      head = window;
      *head = val;
      tail = head;
      inc(tail);
      total = val;
      return;
    }

    // Were we already full?
    if (head == tail) {
      // Fix total-cache
      total -= *head;
      // Make room
      inc(head);
    }

    // Write the value in the next spot.
    *tail = val;
    inc(tail);

    // Update our total-cache
    total += val;
  }

  // Returns the average of the last P elements added to this SMA.
  // If no elements have been added yet, returns 0.0
  double avg() const {
    ptrdiff_t size = this->size();
    if (size == 0) {
      return 0; // No entries => 0 average
    }
    return total / (double) size; // Cast to double for floating point arithmetic
  }

private:
  unsigned int period;
  int * window; // Holds the values to calculate the average of.

  // Logically, head is before tail
  int * head; // Points at the oldest element we've stored.
  int * tail; // Points at the newest element we've stored.

  long total; // Cache the total so we don't sum everything each time.

  // Bumps the given pointer up by one.
  // Wraps to the start of the array if needed.
  void inc(int * & p) {
    if (++p >= window + period) {
      p = window;
    }
  }

  // Returns how many numbers we have stored.
  ptrdiff_t size() const {
    if (head == NULL)
      return 0;
    if (head == tail)
      return period;
    return (period + tail - head) % period;
  }
};

#endif
