
local u = {}
function u.push(q, v)
    if q.tail then  -- task queue is not empty
        q.tail.nxt = v
    else
        q.head = v
    end
    q.tail = v
    local qn = q.n
    if qn then
        q.n = qn + 1
    else
        q.n = 1
    end
    v.inq = 1
end
function u.pop(q)
    if not q then return nil end
    local v = q.head
    if v then
        if q.head == q.tail then -- queue is empty
            q.head = nil
            q.tail = nil
            q.n = 0
        else
            q.head = q.head.nxt
            q.n = q.n - 1
        end
        v.nxt = nil
        v.inq = nil
    end
    return v
end
function u.top(q)
    if not q then return nil end
    return q.head
end
function u.isEmpty(q)
    return not q
end
function u.size(q)
    if not q or not q.n then
        return 0
    end
    return q.n
end

--
local idCnt = 1
function u.genid()
    local id = idCnt
    idCnt = idCnt + 1
    if idCnt >= 0x7FFFFFFF then
        idCnt = 1
    end
    return id
end

return u
