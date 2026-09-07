#ifndef QUEUE_H
#define QUEUE_H

#include <stdexcept>

template <typename T>
class Queue {
private:
    struct Node {
        T data;
        Node* next;
        Node(const T& val) : data(val), next(nullptr) {}
    };

    Node* head;   
    Node* tail;   
    int   count;

public:
    Queue() : head(nullptr), tail(nullptr), count(0) {}

    ~Queue() {
        while (!isEmpty()) dequeue();
    }

    // Copy  constr
    Queue(const Queue& other) : head(nullptr), tail(nullptr), count(0) {
        Node* cur = other.head;
        while (cur) {
            enqueue(cur->data);
            cur = cur->next;
        }
    }

    // Copy assi
    Queue& operator=(const Queue& other) {
        if (this != &other) {
            while (!isEmpty()) dequeue();
            Node* cur = other.head;
            while (cur) {
                enqueue(cur->data);
                cur = cur->next;
            }
        }
        return *this;
    }

    void enqueue(const T& item) {
        Node* newNode = new Node(item);
        if (tail) tail->next = newNode;
        else       head = newNode;
        tail = newNode;
        count++;
    }

    T dequeue() {
        if (isEmpty()) throw std::underflow_error("Queue is empty");
        Node* temp = head;
        T val = temp->data;
        head = head->next;
        if (!head) tail = nullptr;
        delete temp;
        count--;
        return val;
    }

    const T& front() const {
        if (isEmpty()) throw std::underflow_error("Queue is empty");
        return head->data;
    }

    bool isEmpty() const { return count == 0; }
    int  size()    const { return count; }
};

#endif // QUEUE_H
