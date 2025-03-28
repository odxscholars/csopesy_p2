#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <condition_variable>
#include <chrono>
#include <random>
#include <atomic>

using namespace std;

// Player pool
int tanks, healers, dps;
int t1, t2; // dungeon run time bounds

atomic<bool> stopFlag{false};

mutex globalMutex;
condition_variable cv_scheduler;

struct Instance {
    int id;
    bool hasParty = false;
    bool running = false;
    int partiesServed = 0;
    int totalTime = 0;

    int currentTimeElapsed = 0;
    int currDungeonDuration = 0;
    condition_variable cv;
    thread worker;

    Instance(int id_) : id(id_) {
    }

    Instance(const Instance &) = delete;

    Instance &operator=(const Instance &) = delete;

    Instance(Instance &&) = delete;

    Instance &operator=(Instance &&) = delete;
};

vector<shared_ptr<Instance> > instances;

// Random run time between t1 and t2
int getRandomTime() {
    static random_device rd;
    static mt19937 gen(rd());
    uniform_int_distribution<> dist(t1, t2);
    return dist(gen);
}

// Thread function for each dungeon instance
void instanceThread(shared_ptr<Instance> instance) {
    unique_lock<mutex> lock(globalMutex);
    while (!stopFlag) {
        instance->cv.wait(lock, [&]() {
            return instance->hasParty || stopFlag;
        });

        if (stopFlag) break;

        int duration = getRandomTime();
        instance->running = true;
        instance->currDungeonDuration = duration;

        cout << "[Instance " << instance->id << "] Running dungeon for " << duration << " seconds.\n";

        lock.unlock();
        this_thread::sleep_for(chrono::seconds(1));
        instance->currentTimeElapsed++;
        for (int i = 1; i < duration; ++i) {
            this_thread::sleep_for(chrono::seconds(1));
            instance->currentTimeElapsed++;
        }
        lock.lock();

        instance->running = false;
        instance->hasParty = false;
        instance->partiesServed++;
        instance->totalTime += duration;
        instance->currentTimeElapsed = 0;

        cout << "[Instance " << instance->id << "] Dungeon completed.\n";

        // Notify scheduler there's a free instance now
        cv_scheduler.notify_all();
    }
}

// Dedicated scheduler thread: checks for party + instance, assigns work

void schedulerThread() {
    unique_lock<mutex> lock(globalMutex);
    while (true) {
        cv_scheduler.wait(lock, [&]() {
            // cout << "[Scheduler] Woke up.\n";

            return (tanks >= 1 && healers >= 1 && dps >= 3) || stopFlag || any_of(
                       instances.begin(), instances.end(), [](auto &inst) {
                           return !inst->hasParty && !inst->running;
                       }) || instances.empty();
        });

        if (stopFlag) break;

        bool partyAssigned = false;

        // Try to assign party to any free instance
        for (auto &instance: instances) {
            if (!instance->hasParty && !instance->running &&
                tanks >= 1 && healers >= 1 && dps >= 3) {
                cout << "[Scheduler] Assigning party to instance " << instance->id << endl;

                tanks--;
                healers--;
                dps -= 3;

                instance->hasParty = true;
                instance->cv.notify_one();

                partyAssigned = true;
                break; // assign one party per scheduler cycle
            }
        }

        // Check stop condition
        bool hasMorePlayers = (tanks >= 1 && healers >= 1 && dps >= 3);
        // cout << "Tanks: " << tanks << ", Healers: " << healers << ", DPS: " << dps << endl;
        // cout << "[Scheduler] More players: " << (hasMorePlayers ? "yes" : "no") << endl;
        bool anyRunning = false;
        for (auto &inst: instances) {
            if (inst->hasParty || inst->running) {
                // cout << "[Scheduler] Instance " << inst->id << " has party or running.\n";
                anyRunning = true;
                break;
            }
        }

        if (!hasMorePlayers && !anyRunning) {
            stopFlag = true;
            for (auto &inst: instances) inst->cv.notify_all();
            break;
        }

        if (!partyAssigned) {
            // Wait a short time to avoid tight loop if no free instance
            lock.unlock();
            this_thread::sleep_for(chrono::milliseconds(100));
            lock.lock();
        }
    }
}

int main() {
    int n;
    cout << "Enter number of dungeon instances: ";
    cin >> n;
    cout << "Enter number of tanks: ";
    cin >> tanks;
    cout << "Enter number of healers: ";
    cin >> healers;
    cout << "Enter number of DPS: ";
    cin >> dps;
    cout << "Enter min dungeon time (t1): ";
    cin >> t1;
    cout << "Enter max dungeon time (t2): ";
    cin >> t2;

    // Create instances and start threads
    for (int i = 1; i <= n; ++i) {
        auto inst = make_shared<Instance>(i);
        inst->worker = thread(instanceThread, inst);
        instances.push_back(inst);
    }

    // Start scheduler
    thread scheduler(schedulerThread);

    // Monitor loop (display status every second)
    while (!stopFlag) {
        {
            // lock_guard<mutex> lock(globalMutex);
            cout << "\n[Status]\n";
            for (auto &inst: instances) {
                cout << "Instance " << inst->id << ": " << (inst->running
                                                                ? "active (" + to_string(inst->currentTimeElapsed) + "/"
                                                                  + to_string(inst->currDungeonDuration) + ")"
                                                                : "empty") << endl;
            }
            cout << "Leftover players: Tanks: " << tanks << ", Healers: " << healers << ", DPS: " << dps << endl;
            //check if scheduler thread is running
            // cout << "Scheduler: " << (scheduler.joinable() ? "active" : "stopped") << endl;
        }
        this_thread::sleep_for(chrono::milliseconds(1000));
    }

    scheduler.join();
    for (auto &inst: instances)
        if (inst->worker.joinable())
            inst->worker.join();

    // Final summary
    cout << "\n=== Summary ===\n";
    for (auto &inst: instances) {
        cout << "Instance " << inst->id << " served " << inst->partiesServed
                << " parties, total time: " << inst->totalTime << " seconds.\n";
    }

    return 0;
}
