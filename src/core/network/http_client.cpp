#include <core/base/format.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>

#include "core/network/curl_global.hpp"

namespace b_launcher_base::net
{

namespace
{

constexpr log::logger s_log ("http");

struct easy_holder
{
  CURL *handle = nullptr;

  ~easy_holder ()
  {
    if (handle)
      curl_easy_cleanup (handle);
  }
};

CURL *
thread_easy ()
{
  thread_local easy_holder holder;
  if (!holder.handle)
    holder.handle = curl_easy_init ();
  else
    curl_easy_reset (holder.handle);
  return holder.handle;
}

struct response_body
{
  std::string &data;
  std::size_t limit;
  bool too_large = false;
  const std::function<void (std::string_view)> *on_data = nullptr;
};

std::size_t
write_body (char *data, std::size_t size, std::size_t count, void *user)
{
  auto &body = *static_cast<response_body *> (user);
  std::size_t length = size * count;
  if (body.limit > 0 && length > body.limit - body.data.size ())
    {
      body.too_large = true;
      return 0;
    }
  body.data.append (data, length);
  if (body.on_data && *body.on_data)
    (*body.on_data) (std::string_view (data, length));
  return length;
}

std::size_t
write_header (char *data, std::size_t size, std::size_t count, void *user)
{
  auto *headers = static_cast<std::vector<http_header> *> (user);
  std::string_view line (data, size * count);
  if (line.starts_with ("HTTP/"))
    headers->clear ();
  auto colon = line.find (':');
  if (colon != std::string_view::npos)
    headers->push_back ({ std::string (trim (line.substr (0, colon))),
                          std::string (trim (line.substr (colon + 1))) });
  return size * count;
}

int
check_cancel (void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
  return static_cast<cancellation_token *> (user)->is_cancelled () ? 1 : 0;
}

}

std::string_view
http_response::header (std::string_view name) const
{
  for (const auto &h : headers)
    if (iequals (h.name, name))
      return h.value;
  return {};
}

result<http_response>
http_send (const http_request &request, cancellation_token token)
{
  BLB_TRY (token.check ());
  detail::ensure_curl ();
  auto settings = get_network_settings ();
  auto proxy = detail::proxy_for (settings);
  CURL *easy = thread_easy ();
  if (!easy)
    return make_error (errc::network, "cannot create curl handle");

  http_response response;
  response_body body { response.body, request.max_response_size, false,
                       &request.on_data };
  char error_buffer[CURL_ERROR_SIZE] = {};
  curl_slist *headers = nullptr;
  for (const auto &h : request.headers)
    headers = curl_slist_append (headers,
                                 sformat ("{}: {}", h.name, h.value).c_str ());

  detail::apply_common_options (easy, settings, proxy);
  curl_easy_setopt (easy, CURLOPT_SHARE, detail::shared_handle ());
  curl_easy_setopt (easy, CURLOPT_URL, request.url.c_str ());
  curl_easy_setopt (easy, CURLOPT_ERRORBUFFER, error_buffer);
  curl_easy_setopt (easy, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt (easy, CURLOPT_FOLLOWLOCATION,
                    request.follow_redirects ? 1L : 0L);
  curl_easy_setopt (easy, CURLOPT_TIMEOUT_MS,
                    static_cast<long> (request.timeout.count ()));
  curl_easy_setopt (easy, CURLOPT_WRITEFUNCTION, write_body);
  curl_easy_setopt (easy, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt (easy, CURLOPT_HEADERFUNCTION, write_header);
  curl_easy_setopt (easy, CURLOPT_HEADERDATA, &response.headers);
  curl_easy_setopt (easy, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt (easy, CURLOPT_XFERINFOFUNCTION, check_cancel);
  curl_easy_setopt (easy, CURLOPT_XFERINFODATA, &token);
  if (headers)
    curl_easy_setopt (easy, CURLOPT_HTTPHEADER, headers);
  if (request.method == "POST")
    {
      curl_easy_setopt (easy, CURLOPT_POST, 1L);
      curl_easy_setopt (easy, CURLOPT_POSTFIELDSIZE_LARGE,
                        static_cast<curl_off_t> (request.body.size ()));
      curl_easy_setopt (easy, CURLOPT_POSTFIELDS, request.body.data ());
    }
  else if (request.method == "HEAD")
    curl_easy_setopt (easy, CURLOPT_NOBODY, 1L);
  else if (request.method != "GET")
    {
      curl_easy_setopt (easy, CURLOPT_CUSTOMREQUEST, request.method.c_str ());
      if (!request.body.empty ())
        {
          curl_easy_setopt (easy, CURLOPT_POSTFIELDSIZE_LARGE,
                            static_cast<curl_off_t> (request.body.size ()));
          curl_easy_setopt (easy, CURLOPT_POSTFIELDS, request.body.data ());
        }
    }

  s_log.debug ("{} {}", request.method, request.url);
  auto code = curl_easy_perform (easy);
  long status = 0;
  curl_easy_getinfo (easy, CURLINFO_RESPONSE_CODE, &status);
  if (detail::host_failure (code, status))
    detail::note_host (request.url, false);
  else if (code == CURLE_OK)
    detail::note_host (request.url, true);
  curl_easy_setopt (easy, CURLOPT_HTTPHEADER, nullptr);
  curl_easy_setopt (easy, CURLOPT_ERRORBUFFER, nullptr);
  curl_slist_free_all (headers);
  if (body.too_large)
    return make_error (errc::size_mismatch,
                       sformat ("HTTP response exceeds size limit ({})",
                                  request.url));
  if (code != CURLE_OK)
    return detail::curl_failure (code, error_buffer, request.url);
  curl_easy_getinfo (easy, CURLINFO_RESPONSE_CODE, &response.status);
  char *effective = nullptr;
  curl_easy_getinfo (easy, CURLINFO_EFFECTIVE_URL, &effective);
  if (effective)
    response.effective_url = effective;
  return response;
}

result<std::string>
http_get (std::string_view url, cancellation_token token)
{
  http_request request;
  request.url = std::string (url);
  BLB_TRY_ASSIGN (auto response, http_send (request, token));
  if (!response.ok ())
    return make_error (errc::http_status,
                       sformat ("HTTP {} ({})", response.status, url));
  return std::move (response.body);
}

result<std::string>
http_get (std::span<const std::string> urls, cancellation_token token)
{
  if (urls.empty ())
    return make_error (errc::invalid_argument, "no url");
  std::vector<std::string> ordered;
  std::vector<std::string> penalized;
  for (const auto &url : urls)
    {
      // Health can change concurrently; classify each source only once.
      if (detail::host_penalized (url))
        penalized.push_back (url);
      else
        ordered.push_back (url);
    }
  ordered.insert (ordered.end (), penalized.begin (), penalized.end ());
  error last;
  for (const auto &url : ordered)
    {
      auto body = http_get (url, token);
      if (body)
        return body;
      last = std::move (body).err ();
      if (last.code == errc::cancelled)
        return last;
      s_log.warn ("request failed, trying next source: {}", last.message);
    }
  return last;
}

result<http_response>
http_post_json (std::string_view url, std::string body,
                std::vector<http_header> headers, cancellation_token token)
{
  http_request request;
  request.method = "POST";
  request.url = std::string (url);
  request.body = std::move (body);
  request.headers = std::move (headers);
  request.headers.push_back ({ "Content-Type", "application/json" });
  request.headers.push_back ({ "Accept", "application/json" });
  return http_send (request, token);
}

result<http_response>
http_post_form (std::string_view url,
                std::span<const std::pair<std::string, std::string>> fields,
                cancellation_token token)
{
  http_request request;
  request.method = "POST";
  request.url = std::string (url);
  request.body = form_encode (fields);
  request.headers.push_back (
      { "Content-Type", "application/x-www-form-urlencoded" });
  request.headers.push_back ({ "Accept", "application/json" });
  return http_send (request, token);
}

}
