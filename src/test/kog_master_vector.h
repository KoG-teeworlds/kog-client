#ifndef TEST_KOG_MASTER_VECTOR_H
#define TEST_KOG_MASTER_VECTOR_H

// A list signed by the python generator and the Rust tool of the master repository (kogmaster-sign test-fixture): the seeds are
// throwaway test values
static const char KOG_MASTER_VECTOR[] = R"VECTOR({
 "root_a_seed": "0101010101010101010101010101010101010101010101010101010101010101",
 "root_b_seed": "0202020202020202020202020202020202020202020202020202020202020202",
 "root_a_pub": "8a88e3dd7409f195fd52db2d3cba5d72ca6709bf1d94121bf3748801b40f6f5c",
 "root_b_pub": "8139770ea87d175f56a35466c34c7ecccb8d8a91b4ee37a25df60f5b8fc9b394",
 "delegated_seed": "0303030303030303030303030303030303030303030303030303030303030303",
 "delegated_pub": "ed4928c628d1c2c6eae90338905995612959273a5c63f93636c14614ac8737d1",
 "env": "dev",
 "now": 1795501000,
 "document": {
  "v": 1,
  "delegation": {
   "payload": "{\"v\":1,\"env\":\"dev\",\"kid\":\"ed4928c6\",\"pub\":\"ed4928c628d1c2c6eae90338905995612959273a5c63f93636c14614ac8737d1\",\"not_before\":1795000000,\"not_after\":1797592000,\"seq\":1}",
   "root": "8a88e3dd",
   "sig": "340e4078de70ebf75845bc9dcacb079a7e8ce281bc717979d61c647cb9a32551ecb693e4de14cec9a18264a6df5cfedf9a6b281ce534e6acaed861a8714c7e0b"
  },
  "list": {
   "payload": "{\"v\":1,\"env\":\"dev\",\"seq\":1795500000,\"issued\":1795500000,\"not_after\":1795503600,\"kid\":\"ed4928c6\",\"masters\":[{\"t\":\"kog\",\"u\":\"https://master.client.dev.kog.tw/v1/dev/servers\"},{\"t\":\"kog\",\"u\":\"https://client.kog.tw/list/dev/servers.json\"},{\"t\":\"ddnet-json\",\"u\":\"https://master1.ddnet.org/ddnet/15/servers.json\"}],\"servers\":[{\"id\":\"dev-1\",\"addresses\":[\"tw-0.6+udp://203.0.113.10:8303\"],\"name\":\"KoG Dev 1\",\"map\":\"Kobra 3\",\"players\":3,\"max\":64,\"community\":\"kog\",\"version\":\"0.6.5\"},{\"id\":\"dev-2\",\"addresses\":[\"tw-0.6+udp://203.0.113.11:8303\",\"tw-0.7+udp://203.0.113.11:8303\"],\"name\":\"KoG Dev 2\",\"map\":\"Linear\",\"players\":0,\"max\":32,\"community\":\"kog\",\"version\":\"0.6.5\"}]}",
   "sig": "3ebd23bfceeabf569d3179cc3349c0f5b16d68d72c69fe9f9c0c11de0aa7e1ad1d2366abea9d0dd8de82ce8234e01ed8130c6f7c88dff8789fe7a6c895d1da0c"
  }
 }
})VECTOR";

#endif
