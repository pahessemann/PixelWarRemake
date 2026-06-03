import http from "k6/http";
import { check, sleep } from "k6";

export const options = {
  vus: 20,
  duration: "30s"
};

export default function () {
  const map = http.get("http://localhost:8080/map");
  check(map, { "map is readable": (r) => r.status === 200 });
  sleep(1);
}

