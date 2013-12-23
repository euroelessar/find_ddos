#include <iostream>
#include <memory>
#include <algorithm>
#include <elliptics/session.hpp>
#include <boost/program_options.hpp>
#include <boost/thread.hpp>
#include <chrono>
#include <csignal>
#include <random>

using namespace ioremap;

std::chrono::microseconds micro_now()
{
	typedef std::chrono::system_clock clock;
	return std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch());
}

enum : long long int {
	micro_total = 1000 * 1000
};

static constexpr size_t time_delimiters[] = { 0, 1, 2, 5, 10, 2, 50, 100, 500, 1000, 2000, 5000 };
static constexpr size_t time_delimiters_count = sizeof(time_delimiters) / sizeof(time_delimiters[0]);

static volatile size_t time_table[2][time_delimiters_count + 1];
static volatile size_t current_table = 0;

static volatile bool need_exit = false;

void noop_callback()
{
}

void result_callback(const std::chrono::microseconds &request_begin)
{
	const auto ms_spent = std::chrono::duration_cast<std::chrono::milliseconds>(micro_now() - request_begin).count();

	const size_t *begin = time_delimiters;
	const size_t *end = time_delimiters + time_delimiters_count;
	const size_t index = std::lower_bound(begin, end, ms_spent) - begin;

	++time_table[current_table][index];
}

void print_tables()
{
	std::string delimiter;
	delimiter.assign(80, '=');

	while (!need_exit) {
		sleep(1);

		const size_t previous_table = current_table;
		current_table = (current_table + 1) & 1;

		size_t total_count = 0;
		std::cout << delimiter << "\n";
		for (size_t i = 0; i < time_delimiters_count; ++i) {
			std::cout << "[" << time_delimiters[i];
			if (i + 1 < time_delimiters_count)
				std::cout << "-" << time_delimiters[i + 1];
			else
				std::cout << "+";

			auto &result = time_table[previous_table][i];
			std::cout << "]\t";
			if (i <= 5)
				std::cout << "\t";

			std::cout << result << "\n";
			total_count += result;
			result = 0;
		}
		std::cout << "total rps: " << total_count << std::endl << std::endl;
	}
}

void attack(elliptics::session session, size_t rps_count)
{
	std::random_device rd;
	std::uniform_int_distribution<size_t> indexes_count(1, 2);
	std::uniform_int_distribution<size_t> id(
		std::numeric_limits<size_t>::min(), std::numeric_limits<size_t>::max());

	std::vector<dnet_raw_id> indexes;

	while (!need_exit) {
		const auto begin = micro_now();
		for (size_t i = 0; !need_exit && i < rps_count; ++i) {
			indexes.resize(indexes_count(rd));

			for (size_t i = 0; i < indexes.size(); ++i) {
				std::generate_n(reinterpret_cast<size_t *>(indexes[i].id),
					sizeof(dnet_raw_id().id) / sizeof(size_t),
					[&id, &rd] () { return id(rd); });
			}

			session.find_any_indexes(indexes).connect(
				std::bind(noop_callback),
				std::bind(result_callback, micro_now()));
		}
		const auto end = micro_now();
		if (end < begin + std::chrono::seconds(1))
			usleep(std::chrono::duration_cast<std::chrono::microseconds>(begin + std::chrono::seconds(1) - end).count());
	}
}

void stop_ddos(int signal)
{
	std::cout << "Handled signal [" << signal << "]" << std::endl;
	need_exit = 1;
}

int main(int argc, char **argv)
{
	std::ios_base::sync_with_stdio(false);

	namespace bpo = boost::program_options;
	signal(SIGINT, stop_ddos);
	signal(SIGTERM, stop_ddos);

	bpo::variables_map vm;
	bpo::options_description generic("Dos options");

	std::vector<std::string> remotes;
	std::vector<int> groups;
	size_t total_rps_count = 1;
	size_t nodes_count = 1;
	size_t threads_count = 1;

	generic.add_options()
		("help", "This help message")
		("remote", bpo::value(&remotes), "Remote elliptics server address")
		("nodes", bpo::value(&nodes_count), "Nodes count")
		("threads", bpo::value(&threads_count), "Threads count per node")
		("group", bpo::value(&groups), "Groups")
		("rps", bpo::value(&total_rps_count), "Requests per second")
		;

	bpo::store(bpo::parse_command_line(argc, argv, generic), vm);
	bpo::notify(vm);

	if (vm.count("help") || remotes.empty()) {
		std::cerr << generic;
		return 1;
	}

	std::vector<std::unique_ptr<elliptics::node>> nodes;
	std::vector<std::unique_ptr<boost::thread>> threads;

	for (size_t i = 0; i < nodes_count; ++i) {
		elliptics::logger logger;
		elliptics::node node(logger);
		node.set_timeouts(5, 60);

		for (const auto &remote : remotes)
			node.add_remote(remote.c_str());

		nodes.emplace_back(new elliptics::node(node));
	}

	size_t rps_count = total_rps_count;
	for (size_t i = 0; i < nodes_count; ++i) {
		elliptics::session session(*nodes[i]);
		session.set_groups(groups);

		size_t node_rps_count = rps_count / (nodes_count - i);
		rps_count -= node_rps_count;

		for (size_t j = 0; j < threads_count; ++j) {
			size_t current_rps_count = node_rps_count / (threads_count - j);
			if (current_rps_count == 0)
				continue;

			threads.emplace_back(new boost::thread(boost::bind(attack, session.clone(), current_rps_count)));
			node_rps_count -= current_rps_count;
		}
	}

	threads.emplace_back(new boost::thread(print_tables));

	for (size_t i = 0; i < threads_count; ++i) {
		threads[i]->join();
	}

	return 0;
}
