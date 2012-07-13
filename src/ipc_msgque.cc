#include <ipc_msgque.hh>

/* [layout]
 *  [header]
 *    - write_index
 *    - read_index
 *  [index]*
 *    - position_index
 *  [position]*
 *    - start_data
 *    - size
 *    - next_data
 *  [data]
 *
 * - size
 * - next
 * - pid
 * - thread_id
 * - timestamp
 */
bool msgque_t::push(const void* data, std::size_t size) {
  return true;
}

msgque_data_t msgque_t::pop() {
  msgque_data_t data(NULL, 0);
  return data;
}
