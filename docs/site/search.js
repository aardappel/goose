// Simple fuzzy search for Goose docs - no external dependencies
(function() {
  let searchIndex = [];
  let searchInput, searchResults, searchContainer;
  
  let loading = false;
  function loadSearchIndex() {
    if (loading) return;
    loading = true;
    fetch(root + searchFile, { cache: 'force-cache' })
      .then(r => r.json())
      .then(data => {
        data.forEach(d => { d.lower = d.content.toLowerCase(); });
        searchIndex = data;
        if (searchInput.value) search(searchInput.value);
      })
      .catch(err => console.error('Failed to load search index:', err));
  }
  
  // Every term must appear. Headings and whole-word hits rank higher.
  function termScore(term, heading, text) {
    let score = 0;
    const h = heading.indexOf(term);
    if (h !== -1) score += (h === 0 ? 12 : 8);
    const t = text.indexOf(term);
    if (t === -1) return h === -1 ? -1 : score;
    score += 1;
    const before = t === 0 ? '' : text[t - 1];
    if (!/[a-z0-9_]/.test(before)) score += 2;
    const after = text[t + term.length] || '';
    if (!/[a-z0-9_]/.test(after)) score += 2;
    return score;
  }

  function scoreMatch(item, terms) {
    const heading = (item.heading || item.title).toLowerCase();
    const text = item.lower;
    let score = 0;
    for (const term of terms) {
      const s = termScore(term, heading, text);
      if (s < 0) return 0;
      score += s;
    }
    if (item.heading === '') score += 0.5;
    return score;
  }

  let currentResults = [];
  let selectedIndex = -1;
  
  function search(query) {
    if (!query || query.trim().length < 2) {
      searchResults.innerHTML = '';
      searchResults.style.display = 'none';
      currentResults = [];
      selectedIndex = -1;
      return;
    }
    
    const terms = query.toLowerCase().split(/\s+/).filter(Boolean);
    currentResults = searchIndex
      .map(item => ({ item, score: scoreMatch(item, terms) }))
      .filter(r => r.score > 0)
      .sort((a, b) => b.score - a.score)
      .slice(0, 15);
    
    selectedIndex = -1;
    
    if (currentResults.length === 0) {
      searchResults.innerHTML = '<div class="search-no-results">No results found</div>';
      searchResults.style.display = 'block';
      return;
    }
    
    searchResults.innerHTML = currentResults.map((r, idx) => {
      const item = r.item;
      const snippet = getSnippet(item.content, query);
      const label = item.heading ? `${item.title} › ${item.heading}` : item.title;
      return `<a href="${root}${item.url}" class="search-result" data-index="${idx}">
        <div class="search-result-title">${escapeHtml(label)}</div>
        ${snippet ? `<div class="search-result-snippet">${escapeHtml(snippet)}</div>` : ''}
      </a>`;
    }).join('');
    searchResults.style.display = 'block';
  }
  
  function updateSelection() {
    const results = searchResults.querySelectorAll('.search-result');
    results.forEach((r, idx) => {
      if (idx === selectedIndex) {
        r.style.background = 'var(--code-bg)';
      } else {
        r.style.background = '';
      }
    });
    if (selectedIndex >= 0 && results[selectedIndex]) {
      results[selectedIndex].scrollIntoView({ block: 'nearest' });
    }
  }
  
  function getSnippet(content, query) {
    const c = content.toLowerCase();
    const terms = query.toLowerCase().split(/\s+/).filter(Boolean);
    let idx = -1;
    for (const t of terms) {
      idx = c.indexOf(t);
      if (idx !== -1) break;
    }
    if (idx === -1) idx = 0;
    const start = Math.max(0, idx - 60);
    const end = Math.min(content.length, idx + 100);
    let snippet = content.substring(start, end);
    if (start > 0) snippet = '...' + snippet;
    if (end < content.length) snippet = snippet + '...';
    return snippet;
  }

  function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
  }
  
  function init() {
    // Create search UI in header
    const header = document.querySelector('header.top');
    if (!header) return;
    
    searchContainer = document.createElement('div');
    searchContainer.className = 'search-container';
    searchContainer.innerHTML = `
      <input type="text" id="doc-search" class="search-input" placeholder="Search docs" autocomplete="off">
      <kbd class="search-kbd">/</kbd>
      <div id="search-results" class="search-results"></div>
    `;
    header.appendChild(searchContainer);
    
    searchInput = document.getElementById('doc-search');
    searchResults = document.getElementById('search-results');
    
    searchInput.addEventListener('input', (e) => {
      loadSearchIndex();
      search(e.target.value);
    });
    
    searchInput.addEventListener('focus', (e) => {
      loadSearchIndex();
      if (e.target.value.length >= 2) {
        search(e.target.value);
      }
    });
    
    // Hide results when clicking outside
    document.addEventListener('click', (e) => {
      if (!searchContainer.contains(e.target)) {
        searchResults.style.display = 'none';
      }
    });
    
    // Handle keyboard navigation
    searchInput.addEventListener('keydown', (e) => {
      if (e.key === 'Escape') {
        searchResults.style.display = 'none';
        searchInput.blur();
        currentResults = [];
        selectedIndex = -1;
        return;
      }
      if (e.key === 'ArrowDown') {
        if (currentResults.length === 0) return;
        e.preventDefault();
        selectedIndex = (selectedIndex + 1) % currentResults.length;
        updateSelection();
      } else if (e.key === 'ArrowUp') {
        if (currentResults.length === 0) return;
        e.preventDefault();
        selectedIndex = (selectedIndex - 1 + currentResults.length) % currentResults.length;
        updateSelection();
      } else if (e.key === 'Enter') {
        if (selectedIndex >= 0 && currentResults[selectedIndex]) {
          e.preventDefault();
          window.location.href = root + currentResults[selectedIndex].item.url;
        }
      }
    });
    
    // Add '/' shortcut to focus search
    document.addEventListener('keydown', (e) => {
      // Don't trigger if user is typing in an input, textarea, or contenteditable
      if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA' || e.target.isContentEditable) {
        if (e.key === 'Escape') {
          e.target.blur();
        }
        return;
      }
      if (e.key === '/') {
        e.preventDefault();
        searchInput.focus();
        searchInput.select();
      }
    });
    
    loadSearchIndex();
  }
  
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
