local function map_profile(record)
  -- Add username and password to returned map.
  -- Could add other record bins here as well.
  return map {username=record.username, password=record.password}
end

function check_password(stream,password)
  local function filter_password(record)
    return record.password == password
  end
  return stream : filter(filter_password) : map(map_profile)
end
